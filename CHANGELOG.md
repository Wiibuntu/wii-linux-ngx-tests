
Wiibuntu Fork:

v0.3.8 wiibuntu
    * Ported baedit from Go to C++
    * Using Techflash's change, instead of "LABEL=" kernel now uses "ROOT="
    * Added Techflash's patches in more modern kernels (Most dont work yet)


::TechflashYT has added MANY patches between the neagic fork and the Wiibuntu fork.::


neagix fork:

v0.3.6 neagix
	* added Gumboot bootloader
	* switched first partition to FAT32

v0.3.5 neagix
	* use LABEL= on root command line to allow booting from SD/USB
	* fixed login on tty (thanks to DeltaResero for reporting)

v0.3.4 neagix
	* working RGBA mode (some console glitches)
	* added example PNG

v0.3.3 neagix
	* fix tty1 display logo

v0.3.2 neagix
	* kernel v3.15.10
	* fixed missing eth0 network configuration
	* added a framebuffer visualization tool (fbv)
	* installed fbset

v0.3.1 neagix
	* added Farter's framebuffer patch
	* aesthetical fix to first boot initialization

v0.3.0 neagix
	* fixed INIT error on ttyS0
	* kernel v3.14.19

v0.2.0 neagix
	* first rootfs+bootmii+disk image+kernel release


v0.1.0 neagix
	* initial kernel images released
