package smuui

import org.hid4java.event.HidServicesEvent
import org.hid4java.{HidDevice, HidManager, HidServicesListener, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala
import smu.{SmuCommand, SmuResponse}

import java.io.{ByteArrayInputStream, ByteArrayOutputStream}


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

  smuDevice.write(SmuCommand(SmuCommand.Command.SetControl(smu.Control(
    voltage = 1500, currentSource = 250, currentSink = -250,
    enable = true
  ))))

  while (true) {
    smuDevice.write(SmuCommand(SmuCommand.Command.ReadMeasurementsRaw(smu.Empty())))
    println(smuDevice.read())

    Thread.sleep(500)
  }
}
