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
  println(s"Device NV: ${smuDevice.getNvram().toProtoString}")

//  val updateResponse = smuDevice.updateNvram(SmuDevice(serial="1-02"))
  println(s"Device NV: ${smuDevice.getNvram().toProtoString}")

  val calCsv = CSVWriter.open(new File("cal.csv"))
  calCsv.writeRow(Seq("voltageDac", "voltageAdc", "actualVolts"))

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
  readLine()

  for (dacValue <- calDacSequence) {
    var actualVolts: String = ""
    var measurement: smu.MeasurementsRaw = null
    while (actualVolts.isEmpty) {  // allow user to re-send the command
      smuDevice.command(SmuCommand(SmuCommand.Command.SetControlRaw(smu.ControlRaw(
        voltage = dacValue, currentSource = 2042 - 34, currentSink = 2042 + 34,
        enable = true
      ))))
      Thread.sleep(250)
      val response = smuDevice.command(SmuCommand(SmuCommand.Command.ReadMeasurementsRaw(smu.Empty())))
      measurement = response.response.measurementsRaw.get

      println(s"DAC=$dacValue, meas=${measurement.voltage}, enter actual voltage:")

      actualVolts = readLine()
      calCsv.flush()
    }

    calCsv.writeRow(Seq(actualVolts, dacValue.toString, measurement.voltage.toString))
  }

  calCsv.close()
}
