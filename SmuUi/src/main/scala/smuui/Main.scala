package smuui

import org.hid4java.event.HidServicesEvent
import org.hid4java.{HidManager, HidServicesListener, HidServicesSpecification}

import scala.jdk.CollectionConverters.CollectionHasAsScala


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
      smuDevice.write(Seq(0x12, 0x42, i).map(_.toByte).toArray, 3, 0x96.toByte)
      println(s"Read <= ${smuDevice.read().mkString(",")}")
      i = i + 1
    }


  } else {
    println(s"Devices found != 1: ${smuDevices}")
  }

}
