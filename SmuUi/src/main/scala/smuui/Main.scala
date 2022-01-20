package smuui

import org.hid4java.HidServicesSpecification
import org.hid4java.HidManager

import scala.jdk.CollectionConverters.CollectionHasAsScala


object Main extends App {
  val hidServicesSpecification = new HidServicesSpecification
  hidServicesSpecification.setAutoStart(false)
  val hidServices = HidManager.getHidServices(hidServicesSpecification)
  hidServices.start()

  println("HID devices:")
  for (hidDevice <- hidServices.getAttachedHidDevices.asScala) {
    println(hidDevice)
  }
}
