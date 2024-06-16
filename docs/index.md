# wii-linux-ngx

<a href="https://wii-linux.org/"><img alt="Tux + Wii" src="wii-linux-ngx-logo.png" width="160" title="wii-linux-ngx" /></a>

Running modern Linux on Wii consoles.

This project provides:
* source code for a working Linux kernel for GC/Wii, in the form of git branches (named after the Linux kernel version that it's based on)
* modern (2024!), [Void PPC](https://github.com/Void-PPC) based rootfs
* [Gumboot](https://neagix.github.io/gumboot/)
* support for framebuffer RGBA mode

See [README](https://github.com/Wii-Linux/wii-linux-ngx/blob/master/README.md) for a full description and instructions.

# Releases

* https://wii-linux.org

# Issues

Visit the issues tracker to see what needs help:

* See the [Global Timeline](https://github.com/orgs/Wii-Linux/projects/1/views/1)

Pull requests are welcome!

# Frequently Asked Questions

<dl>
<dt>Does *x* USB dongle work?</dt>
<dd>If it was supported upstream around the 4.5 era, it should, if it was supported at the time, but doesn't work here, please <a href="#contact">reach out</a></dd>
<dt>Does the Wi-Fi work?</dt>
<dd>Yes</dd>
<dt>Can I run Xorg on it?</dt>
<dd>Yes</dd>
<dt>Does it work on GameCube?</dt>
<dd>It compiles, but is untested, if you would like to test it, please <a href=#contact>reach out</a></dd>
<dt>Does my SD Card work?</dt>
<dd>Yes</dd>
<dt>Does USB work?</dt>
<dd>Mostly, it can be buggy at times, particularly regarding USB mice, but otherwise is fine</dd>
<dt>What type of graphical applications can I use?</dt>
<dd>Anything that works on a raw framebuffer, or Xorg, will, but anything that requires HW acceleration won't, and <b>nothing</b> demanding high levels of performance, so <b>don't expect any of the following to work</b>:
    <ul>
        <li>Games</li>
        <li>Video playback</li>
        <li>Anything with an update rate > 2FPS</li>
    </ul>
    As a general rule, anything that won't run on a 20 year old laptop without a graphics driver (SW accel), won't run here.
</dd>
</dl>

# Contact

* There's a [Discord server](https://discord.com/invite/D9EBdRWzv2) that is also linked [on the website](https://wii-linux.org) where you can go to chat publicly with the developers and/or other users.
  * For private concerns, such as, but not limited to, business inquiries or urgent security risks, please contact Techflash directly via [email](mailto:officialTechflashYT@gmail.com), or Discord (username: `techflash`).  Note that you are more likely to get a quicker response via Discord than you are Email.

# Thanks

Thanks to the following people/organizations/groups:

* the Linux Kernel developers
* the GC-Linux team
* DeltaResero
* marcan
* neagix (original wii-linux-ngx author)