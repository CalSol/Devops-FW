package smuui

import com.github.tototoshi.csv.CSVWriter
import org.hid4java.event.HidServicesEvent
import org.hid4java.{HidDevice, HidManager, HidServicesListener, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala
import smu.{SmuCommand, SmuResponse}
import device.SmuDevice

import java.io.{ByteArrayInputStream, ByteArrayOutputStream, File}
import scala.io.StdIn.readLine


class SmuInterface(device: HidDevice) {
  device.open()

  override def toString: String = device.toString

  def close(): Unit = {
    device.close()
  }

  // Sends a command and returns the response. Automatically retries in the event no response is received.
  def command(command: SmuCommand): SmuResponse = {
    var response: Option[SmuResponse] = None
    while (response.isEmpty) {
      write(command)
      response = read()
    }
    response.get
  }

  def getDeviceInfo(): smu.DeviceInfo = {
    command(SmuCommand(SmuCommand.Command.GetDeviceInfo(smu.Empty()))).getDeviceInfo
  }

  def getNvram(): SmuDevice = {
    command(SmuCommand(SmuCommand.Command.ReadNvram(smu.Empty()))).getReadNvram
  }

  def updateNvram(nvram: SmuDevice): SmuResponse = {
    command(SmuCommand(SmuCommand.Command.UpdateNvram(value=nvram)))
  }

  // Reads a HID packet and decodes the proto
  protected def read(): Option[SmuResponse] = {
    val readData = device.read().map(_.toByte)
    SmuResponse.parseDelimitedFrom(new ByteArrayInputStream(readData))
  }

  protected def write(command: SmuCommand): Unit = {
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
  println(s"Device opened: $smuDevice")

  println(s"Device info: ${smuDevice.getDeviceInfo().toProtoString}")
  println(s"Read NV: ${smuDevice.getNvram().toProtoString}")

  val updateNvramData = SmuDevice(
//    serial = "1-02",
//    voltageAdcCalibration = Some(device.Calibration(slope = 62.05950631f, intercept = 2034.809922f)),
//    voltageDacCalibration = Some(device.Calibration(slope = -62.35722278f, intercept = 2041.427676f)),

//    serial = "1-01",
//    voltageAdcCalibration = Some(device.Calibration(slope = 62.59849778f, intercept = 2041.890777f)),
//    voltageDacCalibration = Some(device.Calibration(slope = -62.3009151f, intercept = 2047.419768f)),
  )
  println(s"Update NV: ${smuDevice.updateNvram(updateNvramData)} (${updateNvramData.serializedSize} B)")
  println(s"Read NV: ${smuDevice.getNvram().toProtoString}")

  val calCsv = CSVWriter.open(new File("cal.csv"))
  calCsv.writeRow(Seq("measured", "adcVoltage", "adcCurrent", "dacVoltage", "dacCurrentSource", "dacCurrentSink"))

  val kMaxVoltage = 20
  val kMinVoltage = 0

  val kDacCounts = 4095
  val kDacCenter = 2048
  val kVref = 3.0
  def voltageToDac(voltage: Double): Double = {
    val kVoltageRatio = 22.148
    kDacCenter - (voltage * kDacCounts / kVoltageRatio / kVref)
  }
  def currentToDac(current: Double): Double = {
    val kCurrentRatio = 10.000
    kDacCenter - (current * kDacCounts / kCurrentRatio / kVref)
  }
  def getDacSeq(lowValue: Double, highValue: Double, by: Int, convert: Double => Double): Seq[Int] = {
    val lowDac = ((convert(highValue) / by).floor * by).toInt
    val highDac = ((convert(lowValue) / by).ceil * by).toInt
    (lowDac to highDac by by)
  }

  val calDacSequence = Seq(
    // For voltage calibration
//    getDacSeq(0, 20, 64, voltageToDac),
//    getDacSeq(0.25, 5, 16, voltageToDac),
//    getDacSeq(0.25, 1, 4, voltageToDac),

    // For current sink calibration - top out at 2A because the BJT version PNP blew out around 3A
    getDacSeq(-2, 0, 16, currentToDac),
    getDacSeq(-0.5, 0, 4, currentToDac),
  ).flatten.distinct
      .sorted
      .reverse  // do not use in current sink mode
  //      .filter(_ == 1988)

  println(s"${calDacSequence.size} calibration points: ${calDacSequence.mkString(", ")}")
  readLine()

  for (dacValue <- calDacSequence) {
    var measured: String = ""
    var smuMeasured: Option[smu.MeasurementsRaw] = None
    val control = smu.ControlRaw(
      // For voltage calibration
//      voltage = dacValue,
//      currentSource = currentToDac(0.25).toInt, currentSink = currentToDac(-0.25).toInt,

      // For current sink calibration
              voltage = voltageToDac(0).toInt,
              currentSource = currentToDac(0.25).toInt, currentSink = dacValue,
      enable = true
    )

    while (measured.isEmpty || smuMeasured.isEmpty) {  // allow user to re-send the command
      smuDevice.command(SmuCommand(SmuCommand.Command.SetControlRaw(control)))
      Thread.sleep(250)
      val response = smuDevice.command(SmuCommand(SmuCommand.Command.ReadMeasurementsRaw(smu.Empty())))
      smuMeasured = response.response.measurementsRaw
      if (smuMeasured.isDefined) {
        println(s"DAC=$dacValue, meas=${smuMeasured.get}, enter measured:")

        measured = readLine()
        calCsv.flush()
      }
    }

    calCsv.writeRow(Seq(measured,
      smuMeasured.get.voltage.toString, smuMeasured.get.current.toString,
      control.voltage.toString, control.currentSource.toString, control.currentSink.toString))
  }

  calCsv.close()
}
