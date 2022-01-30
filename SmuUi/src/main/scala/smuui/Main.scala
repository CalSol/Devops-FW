package smuui

import com.github.tototoshi.csv.CSVWriter
import com.google.protobuf.CodedInputStream
import org.hid4java.{HidDevice, HidManager, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala
import scalapb.{GeneratedMessage, GeneratedMessageCompanion}
import smu.{SmuCommand, SmuResponse}
import device.SmuDevice

import java.io.{ByteArrayInputStream, ByteArrayOutputStream, File}
import scala.io.StdIn.readLine


// Wrapper around the HID device that implements the serialized-proto-over-reports format
// and supports multi-report chunking.
class HidDeviceProto[WriteType <: GeneratedMessage, ReadType <: GeneratedMessage](
    device: HidDevice, sendReportSize: Int,
    writeObj: GeneratedMessageCompanion[WriteType], readObj: GeneratedMessageCompanion[ReadType]) {
  def write(pb: WriteType): Unit = {
    val outputStream = new ByteArrayOutputStream()
    pb.writeDelimitedTo(outputStream)
    val outputBytes = outputStream.toByteArray

    val packetData = outputBytes.slice(0, math.min(sendReportSize, outputBytes.length))
    val writeResult = device.write(packetData, packetData.length, 0)
    if (writeResult < 0) {
      println(s"Write error $writeResult")  // TODO better logging / error infra
    }
    var bytesWritten: Int = packetData.length

    while (bytesWritten < outputBytes.length) {
      val remainingBytes = outputBytes.length - bytesWritten
      val packetData = (Seq(0.toByte)
          ++ outputBytes.slice(bytesWritten, math.min(sendReportSize - 1, remainingBytes))).toArray
      val writeResult = device.write(packetData, packetData.length, 0)
      if (writeResult < 0) {
        println(s"Write error $writeResult")  // TODO better logging / error infra
      }
      bytesWritten += packetData.length
    }
  }

  def read(): Option[ReadType] = {
    var readData = device.read().map(_.toByte)
    if (readData.isEmpty) {
      println(s"Empty read data (timeout?)")  // TODO better logging / error infra
      return None
    }
    val readStream = new ByteArrayInputStream(readData)  // for decoding the size varint only
    val size = CodedInputStream.readRawVarint32(readStream.read(), readStream)

    var bytesReceived: Int = readData.length
    while (bytesReceived < size) {
      val continuedReadData = device.read().map(_.toByte)
      if (continuedReadData.isEmpty) {
        println(s"Empty read data (timeout?)")  // TODO better logging / error infra
        return None
      }
      if (continuedReadData(0) != 0) {
        println(s"Continued report with first byte != 0")  // TODO better logging / error infra
        return None
      }
      readData = readData ++ continuedReadData.tail
      bytesReceived += continuedReadData.length
    }

    readObj.parseDelimitedFrom(new ByteArrayInputStream(readData))
  }
}


class SmuInterface(device: HidDevice) {
  device.open()
  protected val deviceProto = new HidDeviceProto(device, 64, SmuCommand, SmuResponse)

  override def toString: String = device.toString

  def close(): Unit = {
    device.close()
  }

  // Sends a command and returns the response. Automatically retries in the event no response is received.
  def command(command: SmuCommand): SmuResponse = {
    var response: Option[SmuResponse] = None
    while (response.isEmpty) {
      deviceProto.write(command)
      response = deviceProto.read()
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

  def setNvram(nvram: SmuDevice): SmuResponse = {
    command(SmuCommand(SmuCommand.Command.SetNvram(value=nvram)))
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

  val updateNvramData = SmuDevice(
//    serial = "1-02",
//    voltageAdcCalibration = Some(device.Calibration(slope = 62.1251086354339f, intercept = 2034.62983939408f)),
//    voltageDacCalibration = Some(device.Calibration(slope = -62.5880492671842f, intercept = 2042.21008013648f)),
//    currentAdcCalibration = Some(device.Calibration(slope = 148.587264315509f, intercept = 2032.83594333311f)),
//    currentSourceDacCalibration = Some(device.Calibration(slope = -149.763922762659f, intercept = 2045.90086046013f)),

//    serial = "1-01",
//    voltageAdcCalibration = Some(device.Calibration(slope = 62.59849778f, intercept = 2041.890777f)),
//    voltageDacCalibration = Some(device.Calibration(slope = -62.3009151f, intercept = 2047.419768f)),
//    currentAdcCalibration = Some(device.Calibration(slope = 136.8785618f, intercept = 2042.29232f)),
//    currentSinkDacCalibration = Some(device.Calibration(slope = -137.0626929f, intercept = 2048.703187f)),
  )
  println(s"Device info: ${smuDevice.getDeviceInfo().toProtoString}")
//  println(s"Read NV: ${smuDevice.getNvram().toProtoString}")
//  println(s"Update NV: ${smuDevice.updateNvram(updateNvramData)} (${updateNvramData.serializedSize} B)")
  val readNv = smuDevice.getNvram()
  println(s"Read NV (${readNv.serializedSize}): ${readNv.toProtoString}")

  val kMaxVoltage = 20
  val kMinVoltage = 0

  val kDacCounts = 4095
  val kDacCenter = 2048
  val kVref = 3.0
  val kVoltageRatio = 22.148
  val kCurrentRatio = 10.000
  def voltageToDac(voltage: Double): Double = {
    kDacCenter - (voltage * kDacCounts / kVoltageRatio / kVref)
  }
  def currentToDac(current: Double): Double = {
    kDacCenter - (current * kDacCounts / kCurrentRatio / kVref)
  }
  def dacToCurrent(dac: Int): Double = {
    (kDacCenter - dac).toDouble / kDacCounts * kCurrentRatio * kVref
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

    // For simultaneous calibration using a resistive load
//    getDacSeq(0.25, 10, 64, voltageToDac),
//    getDacSeq(0.25, 2.5, 16, voltageToDac),

    // For current source calibration using a resistive load
    getDacSeq(0, 1, 16, currentToDac),
    getDacSeq(0, 0.25, 4, currentToDac),

    // For current sink calibration - top out at 2A because the BJT version PNP blew out around 3A
//    getDacSeq(-2, 0, 16, currentToDac),
//    getDacSeq(-0.5, 0, 4, currentToDac),
  ).flatten.distinct
      .sorted
      .reverse  // do not use in current sink mode

  println(s"${calDacSequence.size} calibration points: ${calDacSequence.mkString(", ")}")
  readLine()

  val calCsv = CSVWriter.open(new File("cal.csv"))
  calCsv.writeRow(Seq("measured", "adcVoltage", "adcCurrent", "dacVoltage", "dacCurrentSource", "dacCurrentSink"))

  for (dacValue <- calDacSequence) {
    var measured: String = ""
    var smuMeasured: Option[smu.MeasurementsRaw] = None
    val control = smu.ControlRaw(
      // For voltage calibration
//      voltage = dacValue,
//      currentSource = currentToDac(0.25).toInt, currentSink = currentToDac(-0.25).toInt,

      // For simultaneous calibration using a resistive load
//      voltage = dacValue,
//      currentSource = currentToDac(1.25).toInt, currentSink = currentToDac(-0.25).toInt,

      // For current source calibration using a resistive load
      voltage = voltageToDac(dacToCurrent(dacValue) * 11 + 1).toInt,
      currentSource = dacValue,
      currentSink = currentToDac(-0.25).toInt,

      // For current sink calibration
//      voltage = voltageToDac(0).toInt,
//      currentSource = currentToDac(0.25).toInt, currentSink = dacValue,
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
