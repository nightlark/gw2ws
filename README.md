GW2WS
=======
- Demo of what a WebSocket API in GW2 could look like
- Roughly follows the specifications [here](https://gist.github.com/nightlark/d824c172b31d18870d1e)

Download
=======
- Download & extract latest release [here](https://github.com/nightlark/gw2ws/releases)
- Inject the gw2ws.dll into the Guild Wars 2 process using your tool of choice (note: renaming to gw2dps.dll and using gw2dps.exe works)
- Go to (http://nightlark.github.io/gw2ws/) after injecting gw2ws.dll
- Type in {"appName":"TestApp"} (or another name) to get 'authorized' and allow access to the other parts of the API
- Typing in {"enableLocation":true} will update your position in real-time on the webpage
- To exit, hit the Home key

Notes
=======
- Before injecting gw2ws.dll, make sure you are at (or past) the login screen
- This probably only works with the 32-bit client; for a 64-bit version, you will need to build it yourself

![](https://cloud.githubusercontent.com/assets/3969255/12840618/b70cee92-cb99-11e5-9c2f-e82d1e866110.png)
