$port= new-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.Open()
do {Write-Host -NoNewline $port.ReadExisting(); Start-Sleep -Milliseconds 1} while($port.IsOpen)