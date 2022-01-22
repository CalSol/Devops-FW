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

  val kMaxVoltage = 20
  val kMinVoltage = 0

  def voltageToDac(voltage: Double): Double = {
    val kDacCounts = 4095
    val kDacCenter = 2048
    val kVoltRatio = 22.148
    val kVref = 3.0
    kDacCenter - (voltage * kDacCounts / kVoltRatio / kVref)
  }
  def getVoltagesSeq(lowVoltage: Double, highVoltage: Double, by: Int): Seq[Int] = {
    val lowDac = ((voltageToDac(highVoltage) / by).floor * by).toInt
    val highDac = ((voltageToDac(lowVoltage) / by).ceil * by).toInt
    (lowDac to highDac by by)
  }

  val calDacSequence = Seq(
    getVoltagesSeq(0, 20, 64),
    getVoltagesSeq(0.25, 5, 16),
    getVoltagesSeq(0.25, 1, 4),
  ).flatten.distinct
      .sorted.reverse
  //      .filter(_ == 1988)

  println(s"${calDacSequence.size} calibration points: ${calDacSequence.mkString(", ")}")

  for (dacValue <- calDacSequence) {
    var actualVolts: String = ""
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
        println(s"Received $response")
      }
      measurement = response.get.response.measurementsRaw.get

      println(s"DAC=$dacValue, meas=${measurement.voltage}, enter actual voltage:")

      actualVolts = readLine()
      calCsv.flush()
    }

    calCsv.writeRow(Seq(actualVolts, dacValue.toString, measurement.voltage.toString))
  }

  calCsv.close()
}
