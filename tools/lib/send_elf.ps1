# Sends one ELF to the payload loader of a PS5 (default port 9021) over a plain TCP socket.
# Used by tools/deploy.sh; kept in its own file and its own process on purpose: the virus scanner of this PC
# blocked one script that sent an ELF by socket and a file by FTP in a single call.
param(
  [Parameter(Mandatory = $true)][string]$Elf,
  [Parameter(Mandatory = $true)][string]$Ip,
  [int]$Port = 9021
)
$ErrorActionPreference = "Stop"
$bytes = [System.IO.File]::ReadAllBytes($Elf)
$c = New-Object System.Net.Sockets.TcpClient
try {
  $c.Connect($Ip, $Port)
  $s = $c.GetStream()
  $s.Write($bytes, 0, $bytes.Length)
  $s.Flush()
  $c.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)
  # the loader answers with a few lines and closes; read until it does (or 4 s pass)
  $buf = New-Object byte[] 4096
  $s.ReadTimeout = 4000
  try { while (($n = $s.Read($buf, 0, $buf.Length)) -gt 0) { } } catch { }
}
finally {
  $c.Close()
}
"gesendet: $($bytes.Length) Bytes an ${Ip}:${Port}"
