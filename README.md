# RemoteOps — IE3090

Registration number: IT24102925

Personalised values:
- TCP port: 9410 (7000 + first four numeric digits 2410)
- Session ID: SID:5292 (reverse of last four digits 2925)
- Authentication token: OPS-2925
- Sources: agent_925.c, controller_925.c
- Makefile: Makefile_925
- Log: remoteops_IT24102925.log
- Storage: ./agentfiles/IT24102925/
- Archive: IE3090_IT24102925.zip

## Build (Ubuntu/WSL)
```bash
make -f Makefile_925
```

## Run
Terminal 1:
```bash
./agent_925
```
Terminal 2:
```bash
./controller_925 127.0.0.1
```
The controller authenticates automatically. Try SYSINFO, LISTPROC, EXEC DATE, PUT ./sample.txt, GET sample.txt, MONITOR START 9001, MONITOR STOP, and QUIT.

For PUT, provide a local file path visible from the current directory. GET files are saved in `downloads/`.

## Notes
The implementation uses a thread-per-client TCP server and a UDP thread for periodic monitoring. Test on your own system and document only results you actually observe. The fixed EXEC whitelist is intentionally limited to DATE, UPTIME, DISKFREE, HOSTNAME, and WHOAMI.
