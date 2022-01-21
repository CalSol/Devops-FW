package smuui

import org.hid4java.event.HidServicesEvent
import org.hid4java.{HidManager, HidServicesListener, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala
import smu.{SmuCommand, SmuResponse}

import java.io.{ByteArrayInputStream, ByteArrayOutputStream}

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
  var i: Int = 0
  if (smuDevices.size == 1) {
    val smuDevice = smuDevices.head
    println(s"Device found: ${smuDevice}")
    smuDevice.open()
    while (true) {
      val readData = smuDevice.read().map(_.toByte)
      val response = SmuResponse.parseDelimitedFrom(new ByteArrayInputStream(readData))
      println(readData)
      println(response)

      val command = SmuCommand(SmuCommand.Command.SetControl(smu.Control(
        voltage = 1500, currentSource = 100, currentSink = 100,
        enable = false
      )))
      val outputStream = new ByteArrayOutputStream()
      command.writeDelimitedTo(outputStream)
      val outputBytes = outputStream.toByteArray
      smuDevice.write(outputBytes, outputBytes.size, 0)
    }


  } else {
    println(s"Devices found != 1: ${smuDevices}")
  }

}
