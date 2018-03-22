# ClipFilt
IMPORTANT NOTE:

THIS SOURCE CODE, AND THE METHOD IT USES TO MANIPULATE THE CLIPBOARD HANDLING IN AN RDP SESSION, ***IS NOT SUPPORTED IN ANY WAY AT ALL BY MICROSOFT***.  

WHEREAS THE CODE MAY WORK ON EXISTING VERSIONS OF WINDOWS, MICROSOFT MAY CHANGE THE ARCHITECTURE OF THE CLIPBOARD AND/OR RDP COMPONENTS (EITHER IN SECURITY UPDATES, CUMULATIVE ROLLUPS, OR OS UPGRADES) WHICH MAY STOP THE CODE FROM ACHIEVING ITS INTENDED FUNCTION (FILTERING OF THE CLIPBOARD).

That being said, the project uses standard Windows message hooking to achieve granular filtering of the clipboard, when redirected between a client and server RDP session.

The component that manages clipboard redirection in the RDP session (server side) is RDPCLIP.EXE.  RDPCLIP creates a hidden window in order to receive WM_UPDATECLIPBOARD messages, so that it can reflect the clipboard contents down to the client over the RDP protocol (to MSTSC.EXE running on the client; which then takes care of putting the data on the local clipboard).

The host app should be started after  RDPCLIP in the session; at which point it will look for the RDPCLIP hidden window, and attach a GetMsg hook to that window via standard SetWindowsHookEx injection.

When a WM_UPDATECLIPBOARD message is received, the injected hook dll will take action based on different criteria. The code includes samples (commented out) for emptying the clipboard if a particular clipboard format is present.  

Not commented out is sample code to detect whether the clip originated remotely (from the client), by checking the clipboard owner.  If the owner is not RDPClip, the hook dll sends a WM_DESTROYCLIPBOARD message to RDPCLip, so that it believes there is no longer content on the clipboard.   This effectively stops copy/paste from a session to a client; but allows the other way.

Feel free to customise the basic project to provide additional filtering policy for ClipBoard redirection in RDP (remember to compile for x64); taking note of the disclaimer in CAPS above).
