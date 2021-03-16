# HP Fan reporting and control driver

A kernel platform driver to display and control fan speeds on a range of HP ProBook/EliteBook laptops, using ACPI methods.

## **Fair warning!**
This module is currently a proof-of-concept and **not for casual use**!

In particular, fan control is not yet implemented, fan speed reporting is flaky and there are no checks or safeguards.

Embedded Controller hangs and other gremlins are to be expected.

**As of yet, do not use if you don't know what you are doing!**

## Installing
Clone this repo
> git clone https://github.com/cristianchelu/hp_acpi_fan

Build
> cd hp_acpi_fan\
> make

and load the module
> sudo insmod hp_acpi_fan.ko

## Parameters
This module takes two optional parameters, representing the ACPI methods to use when reading/writing fan speed values. If not set, it will try to auto-detect available methods.

> readtype={none,gtmm,gfve,gfrm,kgfs,gfsd,i2cc}\
> ctrltype={auto,stmm,ksfs,kfcl,sfsd,i2cc}

## Testing

You may see the control methods available in your BIOS by disassembling your DSDT table, and then poking around the relevant methods using acpi_call


### Extracting the DSDT
First extract the compiled contents
> cat /sys/firmware/acpi/tables/DSDT > dsdt.dat

then disassemble
> iasl -d dsdt.dat

You should then have the decompiled `dsdt.dsl` file in the current directory.

More information available here: https://wiki.archlinux.org/index.php/DSDT

### Poking around
 - Note! *BEFORE* poking around, make sure to search how to reset the EC of your laptop model, be it by removing the battery or some key combination. Writing unexpected data to the EC or other controllers can cause system instability!

Install the `acpi_call` module by following the instructions here:
https://github.com/nix-community/acpi_call

Search the dsdt.dsl file for methods relating to fan control and reporting. (eg. methods writing to registries named `CFAN` or `PWM0` may be related to fan control). 

Use `acpi_call` to probe said functions and compare their output with expected results. For example:

> echo "\_SB.PCI0.LPCB.EC0.GFSD" > | sudo tee /proc/acpi/call && sudo cat /proc/acpi/call

should return fan speed as a percentage, if such method exists.

likewise,
> echo "\_SB.PCI0.LPCB.EC0.SFSD 50" > | sudo tee /proc/acpi/call && sudo cat /proc/acpi/call

should set fan speed to 50%.

Use your deduction skills and exercise caution.


## ACPI methods

The following methods relating to fan reporting and control were found by disassembling the DSDT tables of an Elitebook 8440p, 8450w, 8460p and x360 1020 G2. They may, however, work on other model-ranges as well, as reported [here](https://ubuntuforums.org/showthread.php?t=2217658).

Not all methods work on all laptops.

### Reporting methods

`\_TZ` methods

On the Elitebook x360 1020 G2, but not on the older models, the following methods are present in the thermal zone region:
- `GTMM` - Debug string expands it to `GetThermalStatus`
  
  Not completely analyzed yet.

- `GFVE` - `GetFanValueExtended`?
  
  Takes one argument in the form `1|2` and returns either `FRDC` or `FR2C` raw value (either Fan0 or Fan1 tachometer value).

- `GTFV` - `GetTargetFanValue`?
  
  Takes one argument in the form `1|2` and returns either `FTGC` or `FT2C` raw value (either Fan0 or Fan1 target speed value).

- `GFRM` - `GetFanRPM`
  
  Takes no arguments. Reads `FRDC` (Fan0 value) and returns it as an RPM value.


`\_SB.PCI0.LPCB.EC` methods
- `GFSD` - `GetFanSpeed(D?)`
  
  Takes no arguments, reads the `PWM0` register and returns its value normalized to 0-100.

  *Note* On the 8440p the fan is controlled by a separate IC, and the PWM value is returned from there.

- `KGFS` - `(K?)GetFanSpeed`
  
  Takes no arguments, reads the `CFAN` register and returns it. Returns `0x14` if the EC is not available

- `KRFS` - `(K?)Get(Right?)FanSpeed`
  
  Takes no arguments, reads the `PFAN` register and returns it. Returns `0x1E` if the EC is not available.

  On the 8540w it is used in the `\_SB.GRFS` method.
  Possibly relevant on laptops equipped with two fans.


### Control methods
`\_TZ` methods

On the Elitebook x360 1020 G2, but not on the older models, the following methods are present in the thermal zone region:

- `STMM` - `SetThermalStatus`.

  Takes one Buffer argument, is pluripotent. Not analyzed yet.

- `CTCT` - Utility method. Uses `MRPM` (MaxRPM) register to convert a `[0-100]` input into a sane value for `FTGC` (Fan0Target).

`\_SB.PCI0.LPCB.EC` methods
- `KFCL` - `(K?)FanControl`
  
  Takes two arguments. Arg0 is checked to be `[0-100]` and written to `CFAN`. Arg1 is *not checked* and written to `MFAC`.

- `KSFS` - `(K?)SetFanSpeed`
  
  Takes one argument, writes it to `CFAN` *unchecked*

- `SFSD` - Takes one argument in `[0-100]`, writes to `PWM0` register.


### Registers
The following `\_SB.PCI0.LPCB.EC` registers are present on all analyzed DSDTs:
- `MFAC` - Minimum fan speed when on AC. Writing to this register seems to have an effect on all tested models.
- `CFAN` - Fan speed control (%?). `KFCL` expects it to be `[0-100]` but other methods do not check.
- `PFAN` - No method writes to this. Presumably secondary fan speed %.
- `PWM0` - bit 1 is "fan-off". bit 8 is unknown, `SFSD` seems to preserve it. bits 2-7 represent fan drive, lower numbers are faster speeds.

The following were only found in the Elitebook 1020:
- `FTGC` - Fan0 value.
- `FT2C` - Fan1 value.
- `FTGC` - Fan0 target value.
- `FT2C` - Fan1 target value.
- `MRPM` - Maximum RPM, used by `CTCT`.

This list will be expanded as more methods are found and understood.

Contributions are welcome!

### Note on the 8440p
The tested laptop has an independent `SMSC EMC2113` fan 
control chip on the I2C bus at addresses 5C/5D, and the control 
method is peculiar. 

The EC initalizes it with a temperature lookup table, but the
provided 'temperature' actually represents the desired PWM value,
even if the chip has a separate PWM input.

The I2C bus and chip are accessible from ACPI via the `\_SB.PCI0.LPCB.SMAB` R/W method.

Consulting the datasheet, it is possible to switch this chip to a 
different LUT or direct control register, completely bypassing the EC.
This, however, is currently beyond the scope of this module.

## TODO
- [ ] Improve documentation
- [ ] Automatic control method detection
- [ ] Reporting PWM vs RPM / Dynamically register HWMON attributes
- [ ] Refactor and make sense of the quantum-zoo of registers
- [ ] MAYBE: i2c fan chip detection and direct control