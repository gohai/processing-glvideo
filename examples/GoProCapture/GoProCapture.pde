/**
 *  This will display live video from a GoPro camera connected via WiFi.
 *  Code & detective work by Gal Nissim & Andres Colubri.
 *
 *  More about GStreamer pipelines:
 *  https://gstreamer.freedesktop.org/data/doc/gstreamer/head/manual/html/
 */

import gohai.glvideo.*;
import java.io.*;
import java.net.*;
GLVideo video;

void setup() {
  size(320, 240, P2D);

  thread("init");

  // we connect to the camera via this custom GStreamer pipeline
  video = new GLVideo(this, "udpsrc port=8554 ! tsdemux ! decodebin", GLVideo.NO_SYNC);
  video.play();

  thread("keepAlive");
}

void draw() {
  background(0);
  if (video.available()) {
    video.read();
  }
  image(video, 0, 0, width, height);
}


// this function initiates the connection with the GoPro
void init() {
  try {
    URL url = new URL("http://10.5.5.9:8080/gp/gpControl/execute?p1=gpStream&c1=restart");
    HttpURLConnection urlConn = (HttpURLConnection) url.openConnection();

    // if the URL takes more then 5 seconds to connect throw exeption
    urlConn.setRequestMethod("HEAD");
    urlConn.setConnectTimeout(5000);

    if (urlConn.getResponseCode() == 200) {
      println("Connected to GoPro");
    }
  }
  catch (Exception e) {
    System.err.println("Error connecting to GoPro");
    System.err.println("Make sure that the GoPro is setup to Wi-Fi mode & re-run the sketch.");
  }
}

// this function prevents the capture from stopping after a couple of seconds
void keepAlive() {
  int KEEP_ALIVE_PERIOD = 2500;

  while (true) {
    try {
      DatagramSocket clientSocket = new DatagramSocket();
      String sendMessage = "_GPHD_:0:0:2:0.000000\n";
      byte[] sendData = sendMessage.getBytes();
      InetAddress IPAddress = InetAddress.getByName("10.5.5.9");
      DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, IPAddress, 8554);
      clientSocket.send(sendPacket);
      clientSocket.close();
    }
    catch (Exception e) {
      System.err.println("Error keeping the connection with GoPro alive");
    }
    delay(KEEP_ALIVE_PERIOD);
  }
}
