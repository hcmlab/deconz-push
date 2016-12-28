Introduction
-----------------------------------

The fhem-deconz-push is an extension for the [deCONZ](http://www.dresden-elektronik.de/funktechnik/products/software/pc/deconz?L=1) REST plugin and creates a tunnel to a local fhem instance (or provides a push socket) in order to instantly push any changes of ZigBee devices (managed by the deCONZ application) to fhem (or the socket).
This help reduce polling load of fhem and change the poll intervall to e.g. 3600 seconds (once an hour).
Much more important, this extension enables using button devices (e.g. Philips Dimmer Switch, Hue Tap) for fhem.
Support for this extension is given at forum.fhem.de

![PushTunnel](/fhem-deconz-tunnel.png)
The figure shows where the extension is located and how it integrates into deCONZ/fhem. ZigBee devices are managed and controlled through deCONZ as the bridge. As soon as something has changed (e.g. a button was pressed, a light has been turned on), the events trigger a message that instantly changes readings (e.g. state, pct, onoff, etc.) of the according HUEDevices in fhem.
Basically, there is no need anymore for polling cycles between fhem and the bridge.


License
-----------------------------------
The plugin is available as open source and licensed under the [Eclipse Public License - Version 1.0](LICENSE.html).


Requirements for FHEM
-----------------------------------
  1. FHEM installation on RaspBerry Pi<br/>
  2. ZigBee devices are managed by deCONZ and included into FHEM as HUEDevice's<br/>
  3. The deCONZ application and FHEM run on the same RaspBerry<br/>
  4. Telnet in fhem is enabled<br/>
  4.1 Authentication/SSL on telnet is NOT supported yet<br/>

Requirements without FHEM
-----------------------------------
  1. ZigBee devices are managed by deCONZ<br/>


Usage
-----------------------------------
1. Open a terminal (bash) to/at the RaspBerry (e.g. SSH)


2. Download the project
```bash
git clone https://hcm-lab.de/git/chi-tai/fhem-deconz.git
cd fhem-deconz
```

3. For FHEM: Path to fhem home
If FHEM is not installed in /opt/fhem, then you must provide the path through the following command:
```bash
export FHEM_HOME=/your/path/to/fhem
```

4. Install the plugin
```bash
./install.sh
```
The script installs an extension script (99_myDeconz1.pm) into the FHEM folder, creates a backup of the original rest-plugin, and installs the push extension.


5. OPTIONAL. Instead of using the prebuilt binary, you may build the plugin yourself.<br/>
The build script automatically installs the plugin. So, if you decide to build the plugin by yourself, you can skip the next step (4.).
```bash
./build.sh
```
The script downloads and install all necessary packages to build the plugin and invokes a make afterwards.
If the build was successful the script invokes install.sh afterwards.


6. For FHEM: Restart FHEM<br/>
Enter "shutdown restart" into the fhem commandbox.


7. Restart deCONZ<br/>
Depending on how you installed deCONZ, you need to stop (if it is still running) and start deCONZ.


Push using fhem tunnel
-----------------------------------
Any changes of a rest node result in change of the related readings in the HUEDevices.

Push using socket listener
-----------------------------------
Client listeners have to connect to the IP of the deCONZ plugin and configured port (default: 7073).
Before a push message can be signaled, a client has to send a '1' to indicate its ready state (after all push messages).
If a client sends a '0', then the connection will be terminated.
Any changes of a rest node result in a message that lists the rest node ids. For example:

```bash
l: 1,2,3
s: 1
g: 1
```
In this example, the light nodes 1, 2, and 3 have changed; the sensor node 1 has changed; the group node 1 has changed.


Remove the plugin
-----------------------------------
The install script automatically creates a backup of the original plugin in the "bkp"-folder. The following command restores the backup and removes the fhem-deconz-push extension.
```bash
./uninstall.sh
```

Configuration through FHEM
-----------------------------------
By means of the following readings (if available), the extension can be explicitly configured. The readings have to be available in the HUEBridge that connects to the deCONZ bridge. Boolean values are represented as 1 (true) or 0 (false). The given values here are the default values.

- Enable the push extension.
```bash
setreading deCONZBridge push 1
```

- Enable the push listener socket.
```bash
setreading deCONZBridge pushSocket 0
```

- Enable the fhem tunnel.
```bash
setreading deCONZBridge fhemtunnel 1
```

- Telnet port to fhem.
```bash
setreading deCONZBridge fhemPort 7072
```

- Push (listener) port for client conects.
```bash
setreading deCONZBridge pushPort 7073
```


Configuration without FHEM
-----------------------------------
The extension can also be configured by means of a configuration file located at
```bash
/usr/share/deCONZ/rest_push.txt
```

The following options are available:
```bash
# FHEM telnet port
2 fport 7072

# Push client socket port
2 pport 7073

# Disable plugin
#0 disable

# Disable fhem tunnel
#0 disablefhem

# Disable push listener socket
#0 disablepush
```
All the options above relate to the meaning given in the previous section. If an option is not given in the configuration file, then the options translates to enabled (true).

