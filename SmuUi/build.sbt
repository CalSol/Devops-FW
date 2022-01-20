name := "smu-ui"

version := "0.1"

scalaVersion := "2.13.5"

scalacOptions += "-deprecation"

libraryDependencies ++= Seq(
  "org.scalafx" %% "scalafx" % "16.0.0-R25",
  "com.github.tototoshi" %% "scala-csv" % "1.3.8",

  "org.scalatest" %% "scalatest" % "3.2.0" % "test",
  
  "org.hid4java" %% "hid4java" % "0.7.0", 
)

// JavaFX binary detection, from https://github.com/scalafx/ScalaFX-Tutorials/blob/master/hello-sbt/build.sbt
val javafxModules = Seq("base", "controls", "fxml", "graphics", "media", "swing", "web")
val osName = System.getProperty("os.name") match {
  case n if n.startsWith("Linux") => "linux"
  case n if n.startsWith("Mac") => "mac"
  case n if n.startsWith("Windows") => "win"
  case _ => throw new Exception("Unknown platform!")
}
libraryDependencies ++= javafxModules.map(m => "org.openjfx" % s"javafx-$m" % "16" classifier osName)

assembly / assemblyMergeStrategy := {
  case "module-info.class" => MergeStrategy.first
  case x =>
    val oldStrategy = (assembly / assemblyMergeStrategy).value
    oldStrategy(x)
}
