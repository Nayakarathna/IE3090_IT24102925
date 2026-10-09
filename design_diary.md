# Design Diary — RemoteOps (complete with actual dates and decisions)

## Initial setup
Installed Ubuntu under WSL and prepared a project directory. Package installation initially failed because APT repository URLs returned HTTP 403 errors. Changed Ubuntu repository URLs from HTTP to HTTPS and confirmed that `apt update` completed successfully.

## Implementation decisions
- Language/API: C with the BSD/POSIX socket API, as required by the brief.
- Concurrency: one detached pthread per TCP client, so multiple controllers can be served concurrently.
- TCP framing: read text lines separately and transfer file payloads by exact byte count.
- UDP monitoring: send system-statistics datagrams periodically to the Controller's IP and selected UDP port.
- Security: require the personalised token before commands and restrict EXEC to the five specified commands.
- File storage: use the personalised `agentfiles/IT24102925/` directory and reject path separators in uploaded/downloaded filenames.

## Testing and obstacles
Fill in the actual date, command, expected output, actual output, and fix for every test performed. Add any additional obstacles or design changes discovered during testing. Do not record tests as passed until they have been run.
