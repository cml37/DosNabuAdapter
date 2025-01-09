# DOSNabuAdapter
DOS version of the NABU Adapter

# Building
Use OpenWatcom 1.9 (DO NOT USE OpenWatcom 2.0 beta, you'll get heap corruption issues with `fopen()` calls)

# Running
* Copy to your DOS PC
* Copy NABU cycles that contain PAK files to C:\cycle or a location of your choice, or configure your system to use mTCP
* Run the application (pass in the number of your serial port and an optional cycle path if not C:\cycle, as well as an optional http host and path to NABU cycles if you have a specific cycle you'd like to pull from online)

* The application will look for either .nab or .pak files in the cycle directory specified
  * If said .nab or .pak file cannot be found, it will attempt to download it from the internet based on the http host and path specified
    * NOTE: The host must support http, this application will NOT use https for download
* If any issues arise with loading files, suggest clearing out any files in the cycle directory
