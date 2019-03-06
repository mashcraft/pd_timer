# Raspberry Pi Kiosk setup

1. Start with fresh Raspbian 

1. Boot and let it re-size the file system.

1. Set localization as desired.

1. Set password for user pi

1. Connect to the Internet and update.

1. Disable Screensaver - See https://www.raspberrypi.org/documentation/configuration/screensaver.md
  
  1. ```sudo vi /boot/cmdline.txt```
  2. add "consoleblank=0"
  3. Install xscreensaver ```sudo apt-get install xscreensaver```
  4. Launch in x under preferences and disable screensaver.

1. Install the derbynet extras - See https://jeffpiazza.org/derbynet/builds/docs/Installation-%20Debian.pdf

```sudo apt-get install -y apt-transport-https
wget -q -O- https://jeffpiazza.org/derbynet/debian/jeffpiazza_derbynet.gpg | sudo apt-key add -
echo "deb [arch=all] https://jeffpiazza.org/derbynet/debian stable main" | sudo tee /etc/apt/sources.list.d/derbynet.list > /dev/null
sudo apt-get update
```

1. Install derbynet extras
```sudo apt-get install derbynet-extras```

1. Setup kiosk autostart
```
mkdir ~/.config/autostart/
cp /usr/share/derbynet/autostart/kiosk.desktop ~/.config/autostart
```
Update settings ```sudo vi /etc/derbynet.conf```
 DERBYNET_SERVER should be the url to your derbynet host
 ADDRESS should be something unique for easy identification and management
 
1. Connect to the Wi-Fi network you will use at the race.

1. reboot

1. Test
