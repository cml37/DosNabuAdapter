# DOSNabuAdapter
DOS version of the NABU Adapter

# Building
Use OpenWatcom 2.0 beta

# Running
* Copy to your DOS PC
* Copy NABU cycles that contain PAK files to C:\cycle or a location of your choice, or configure your system to use mTCP
* Run the application (pass in the number of your serial port and an optional cycle path if not C:\cycle, as well as an optional http host and path to NABU cycles if you have a specific cycle you'd like to pull from online)

# Issues
* Heap corruption apppears whenever `fopen()` is called, but issue probably lies elsewhere
  * Program functions normally, but it's possible that a reboot may be required with subsequent runs