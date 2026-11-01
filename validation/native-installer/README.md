# ROM native SIS installation check

This probe starts the ROM Software Installer (UID 0x101f875a) on
`E:\Data\ngage-device-test.sis` using RApaLsSession. The N-Gage SIS itself must
be installed through that UI, not through EKA2L1's host `POST /v1/apps` endpoint.
Only the probe's small package may be installed through the host endpoint.

Build/package like native/touch using symbian-platform 0.3.0; copy the candidate
SIS to that guest path, launch probe UID 0xe0000990, and accept the install and
drive-selection dialogs. A passing candidate has no compatibility warning,
completes the installation, writes the expected C/E files and SIS registry entries,
and starts Games (Touch) after a cold restart. Capture each transition.

Use an isolated C/E profile with ROM stubs registered and no prior N-Gage client.
On the supplied custom X7 ROM, its existing hacked YnstallServer fixture is a
prerequisite for the ROM UI, not part of the ordinary-phone candidate package.

The version 1.30(1542) ordinary-phone package passed this sequence on X7/Belle.
Nokia 808 Belle FP2 physical-device confirmation is still pending.
