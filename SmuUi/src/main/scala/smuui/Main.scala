package smuui

import com.github.tototoshi.csv.CSVWriter
import org.hid4java.event.HidServicesEvent
import org.hid4java.{HidDevice, HidManager, HidServicesListener, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala
import smu.{SmuCommand, SmuResponse}

import java.io.{ByteArrayInputStream, ByteArrayOutputStream, File}
import scala.io.StdIn.readLine


class SmuInterface(device: HidDevice) {
  device.open()

  override def toString: String = device.toString

  def close(): Unit = {
    device.close()
  }

  // Reads a HID packet and decodes the proto
  def read(): Option[SmuResponse] = {
    val readData = device.read().map(_.toByte)
    SmuResponse.parseDelimitedFrom(new ByteArrayInputStream(readData))
  }

  def write(command: SmuCommand): Unit = {
    val outputStream = new ByteArrayOutputStream()
    command.writeDelimitedTo(outputStream)
    val outputBytes = outputStream.toByteArray
    device.write(outputBytes, outputBytes.size, 0)
  }
}


object Main extends App {
  val kDeviceVid = 0x1209
  val kDevicePid = 0x07  // TODO this is a temporary unallocated PID

  val hidServicesSpecification = new HidServicesSpecification
  hidServicesSpecification.setAutoStart(false)
  val hidServices = HidManager.getHidServices(hidServicesSpecification)
  hidServices.start()

  val smuDevices = hidServices.getAttachedHidDevices.asScala.filter { device =>
    device.getVendorId == kDeviceVid && device.getProductId == kDevicePid && device.getProduct == "USB PD SMU"
  }

  if (smuDevices.size != 1) {
    println(s"Devices found != 1: ${smuDevices}")
    System.exit(1)
  }

  val smuDevice = new SmuInterface(smuDevices.head)
  println(s"Device opened: ${smuDevice}")

  val calCsv = CSVWriter.open(new File("cal.csv"))
  calCsv.writeRow(Seq("voltageDac", "voltageAdc", "actualVolts"))

  val calDacSequence = Seq(
    1536,
    1600,
    1664,
    1728,
    1792,
    1856,
    1920,
    1952,
    1984,
    2000,
    2016,
    2024,

    // below are off-scale low
//    2032,

//    2040,
//    2044,
//
//    2046,
//    2047,
//    2048,
//    2049,
//    2050,
//
//    2052,
//    2056,
//    2064,
  )

  for (dacValue <- calDacSequence) {
    var actualVolts: Option[Float] = None
    var measurement: smu.MeasurementsRaw = null
    while (actualVolts.isEmpty) {  // allow user to re-send the command
      smuDevice.write(SmuCommand(SmuCommand.Command.SetControlRaw(smu.ControlRaw(
        voltage = dacValue, currentSource = 2042 - 34, currentSink = 2042 + 34,
        enable = true
      ))))
      Thread.sleep(250)
      smuDevice.write(SmuCommand(SmuCommand.Command.ReadMeasurementsRaw(smu.Empty())))

      var response: Option[SmuResponse] = None
      while (response.isEmpty || !response.get.response.isMeasurementsRaw) {
        response = smuDevice.read()
      }
      measurement = response.get.response.measurementsRaw.get

      println(s"DAC=$dacValue, meas=${measurement.voltage}, enter actual voltage:")

      actualVolts = readLine().toFloatOption
    }

    calCsv.writeRow(Seq(dacValue.toString, measurement.voltage.toString, actualVolts.get.toString))
  }

  calCsv.close()
}
