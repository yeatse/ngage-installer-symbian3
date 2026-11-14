# Import probe

A standalone app (UID 0xe000099c, no capabilities) for a phone whose loader
refuses a binary with KErrNotSupported. It imports only euser and efsrv, loads
the libraries the posting DLL depends on by name, and writes to
`C:\Data\import_probe.txt`:

- where each library was found and how many ordinals it exports;
- whether each ordinal the posting DLL imports resolves;
- the first bytes of code behind ws32 ordinals 560 to 662 and every surface
  manager and update client ordinal, so a function can be identified where a
  phone numbers it differently from the X7;
- the surface manager calls that need no window.

Not part of the phone SIS. Build and package like validation/rg-load-probe.
