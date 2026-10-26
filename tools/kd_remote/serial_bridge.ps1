# Runs on the OTHER PC. TCP 127.0.0.1:Port (the ssh reverse tunnel to COM8 on the main PC) <-> virtual COM31 (com0com pair partner of COM30).
# kd then uses plain `-k com:port=COM30,baud=115200`, like on the main PC.
param([string]$Com = 'COM31', [int]$Port = 5170, [int]$Baud = 115200)
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.IO.Ports;
using System.Net.Sockets;
using System.Threading;
public static class SerialBridge {
  static void Pump(Stream a, Stream b, ManualResetEvent done) {
    byte[] buf = new byte[4096];
    try { int n; while ((n = a.Read(buf, 0, buf.Length)) > 0) { b.Write(buf, 0, n); b.Flush(); } } catch (Exception) { }
    done.Set();
  }
  public static void Run(string com, int port, int baud) {
    using (SerialPort sp = new SerialPort(com, baud, Parity.None, 8, StopBits.One)) {
      sp.Handshake = Handshake.None;
      sp.DtrEnable = true;
      sp.RtsEnable = true;
      sp.Open();
      Console.WriteLine("[serial] opened " + com);
      using (TcpClient tc = new TcpClient()) {
        tc.NoDelay = true;
        tc.Connect("127.0.0.1", port);
        Console.WriteLine("[serial] tcp connected 127.0.0.1:" + port);
        NetworkStream ns = tc.GetStream();
        ManualResetEvent done = new ManualResetEvent(false);
        Thread t1 = new Thread(delegate() { Pump(sp.BaseStream, ns, done); }); t1.IsBackground = true; t1.Start();
        Thread t2 = new Thread(delegate() { Pump(ns, sp.BaseStream, done); }); t2.IsBackground = true; t2.Start();
        done.WaitOne();
        Console.WriteLine("[serial] link closed");
      }
    }
  }
}
'@ -ReferencedAssemblies System.dll
[SerialBridge]::Run($Com, $Port, $Baud)
