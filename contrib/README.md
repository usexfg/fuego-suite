# xfg-swapd Service Units

## Linux (systemd)

```bash
sudo cp contrib/swapd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now swapd
sudo systemctl status swapd
```

Edit `/etc/systemd/system/swapd.service` to adjust paths and config file locations before starting.

## macOS (launchd)

```bash
sudo cp contrib/com.fuego.swapd.plist /Library/LaunchDaemons/
sudo launchctl load /Library/LaunchDaemons/com.fuego.swapd.plist
```

Check status:
```bash
launchctl list com.fuego.swapd
tail -f /var/log/fuego/swapd.log
```

To unload:
```bash
sudo launchctl unload /Library/LaunchDaemons/com.fuego.swapd.plist
```
