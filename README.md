Introduction
-----------------------------------

The deconz-push is an extension for the [deCONZ](http://www.dresden-elektronik.de/funktechnik/products/software/pc/deconz?L=1) REST plugin that provides a push socket and/or creates a tunnel to a local fhem instance in order to instantly push any changes of ZigBee devices (managed by the deCONZ application) to fhem (or the socket).
This helps reduce polling load of fhem/home-apps and allow to change poll intervalls to e.g. 3600 seconds (once an hour).
Much more important, this extension enables using button devices (e.g. Philips Dimmer Switch, Hue Tap) for home automation.
Support for this extension is given at https://forum.fhem.de/index.php/topic,63614.0.html

![PushTunnel](/fhem-deconz-tunnel.png)
The figure shows where the extension is located and how it integrates into deCONZ/home automation. ZigBee devices are managed and controlled through deCONZ as the bridge. As soon as something has changed (e.g. a button was pressed, a light has been turned on), the according deCONZ events trigger a message that instantly changes readings (e.g. state, pct, onoff, etc.) of the according HUEDevices in fhem or push message to listeners.
Basically, there is no need anymore for polling cycles between home automation apps and the bridge.


License
-----------------------------------
The plugin is available as open source and licensed under the [Eclipse Public License - Version 1.0](LICENSE.html).
<br><br>

Requirements for FHEM
-----------------------------------
  1. FHEM installation on RaspBerry Pi<br/>
  2. ZigBee devices are managed by deCONZ and included into FHEM as type HUEDevice<br/>
  3. The deCONZ application and FHEM run on the same RaspBerry<br/>
  3.1 deCONZ 2.04.35<br/>
  3.2 deCONZ 2.04.18 (checkout branch 2.04.18)<br/>
  4. Telnet in fhem is enabled. Password authentication and/or SSL/TLS must be enabled if the FHEM-IP is other than localhost.<br/>
<br>

Requirements without FHEM
-----------------------------------
  1. ZigBee devices are managed by deCONZ
<br><br>


Usage
-----------------------------------
**1.** Open a terminal (bash) to/at the RaspBerry (e.g. SSH)
<br><br>

**2.** Download the project
```bash
git clone https://hcm-lab.de/git/project/deconz-push.git
cd deconz-push
```
<br>
If you're not using the recent deCONZ release, then checkout a branch that macht your version, e.g.
```bash
git checkout 2.04.18
```

**3.** OPTIONAL. For FHEM: Path to fhem home<br>
If FHEM is not installed in /opt/fhem, then you must provide the path through the following command:
```bash
export FHEM_HOME=/your/path/to/fhem
```
<br>

**4.** Install the plugin
```bash
./install.sh
```
The script installs an extension script (99_myDeconz1.pm) into the FHEM folder, creates a backup of the original rest-plugin, and installs the push extension.
<br><br>

**5.** OPTIONAL. Instead of using the prebuilt binary, you may build the plugin yourself.<br>
The build script automatically installs the plugin. So, if you decide to build the plugin by yourself, you can skip the previous step (4.).
```bash
./build.sh
```
The script downloads and installs all necessary packages to build the plugin and invokes a "make" afterwards.
If the build was successful the script invokes install.sh afterwards.
<br><br>

**6.** For FHEM: Restart FHEM<br>
Enter "shutdown restart" into the fhem commandbox.

Afterwards, it is recommended to create a device using the myDeconz1 module. Otherwise, the module needs to be reloaded in case of a 'rereadcfg' through the fhem commandbox.
```bash
define myDeconz myDeconz1
```
<br><br>

**7.** Restart deCONZ<br>
Depending on how you installed deCONZ, you need to stop (if it is still running) and start deCONZ.
<br><br>


Push using fhem tunnel
-----------------------------------
Any changes of a rest node result in change of the related readings in the HUEDevices.
<br><br>

Push using socket listener
-----------------------------------
Client listeners can connect to the IP of the deCONZ plugin and configured port (default: 7073) to receive push messages. By sending a 'quit' message, the connection terminates.
Any changes of a rest node result in a message that follows the following form:
Values are seperated by a ^ character. The first value specifies the current session, the second value indicates whether a light (l), sensor (s), or a group (g) has changed, the third value specifies the id of the device, the fourth value names the reading that has changed, and the last value represents the actual value. Afterwards, more readings and values may follow that are related to the given device.

```bash
68^l^4^level^254
68^l^4^level^254^xy^0,382037,0,000000
```
In this example, the light node 4 has changed; The level has changed to 254 and the xy reading has changed to 0,382037,0,000000.
<br>

Remove the plugin
-----------------------------------
The install script automatically creates a backup of the original plugin in the "bkp"-folder. The following command restores the backup and removes the fhem-deconz-push extension.
```bash
./uninstall.sh
```
<br>

Configuration through FHEM
-----------------------------------
By means of the following readings (if available), the extension can be explicitly configured. The readings have to be available in the HUEBridge that connects to the deCONZ bridge. Boolean values are represented as 1 (true) or 0 (false). The given values here are the default values.

- Enable the push extension.
```bash
setreading deCONZBridge push 1
```

- Disable the push listener socket. This is only required, if you are using external push processes. Currently no one is known, so it is safe to disable this option.
```bash
setreading deCONZBridge pushSocketListener 0
```

- Enable the fhem tunnel.
```bash
setreading deCONZBridge fhemtunnel 1
```

- Telnet port to fhem.
```bash
setreading deCONZBridge fhemPort 7072
```

- FHEM telnet enable ssl (mandatory for an IP other than localhost)
```bash
setreading deCONZBridge ssl 1
```

- FHEM telnet password (mandatory for an IP other than localhost)
```bash
setreading deCONZBridge fpass password
```

- Push (listener) port for client conects.
```bash
setreading deCONZBridge pushPort 7073
```
<br>
After changing one of theses readings, deCONZ needs to be restarted for the readings to become effective. 
<br><br>


Configuration without FHEM
-----------------------------------
The extension can also be configured by means of a configuration file located at
```bash
/usr/share/deCONZ/rest_push.conf
```

The following options are available:
```bash
# Disable plugin
#disableplugin

# FHEM telnet port
fport 7072

# FHEM telnet IP address
fip 127.0.0.1

# FHEM telnet enable ssl (mandatory for an IP other than localhost)
#ssl

# FHEM telnet password (mandatory for an IP other than localhost)
#fpass password

# Push client socket port
pport 7073

# Disable fhem tunnel
#disablefhem

# Disable push listener socket
#disablepushlistener
		
nonodeupdate
```
All the options above relate to the meaning given in the previous section. If an option is not given in the configuration file, then the option translates to the default (or enabled (true)).

