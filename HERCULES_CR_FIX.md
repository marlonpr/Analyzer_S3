# Hercules CR command terminator fix

The previous analyzer accepted commands only when a line-feed (`LF`, `0x0A`) arrived and silently ignored carriage return (`CR`, `0x0D`). Hercules is commonly configured to send `<CR>`.

This version accepts all three common endings:

- `CR`
- `LF`
- `CRLF`

Therefore these Hercules send buttons work directly:

```text
BEGIN|1|1<CR>
STATUS<CR>
END|1|1<CR>
```

Expected responses:

```text
ANZ|ACK|BEGIN|1|1
ANZ|STATUS|armed=1|run=1|trial=1|...
ANZ|SUMMARY|1|1|...
ANZ|COUNTS|1|1|...
```
