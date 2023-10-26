_RegDev_ EPICS Device Support
=============================

This generic EPICS device support is intended to connect arbitrary register
based devices to EPICS in a simple and flexible way. It can be used for
rapid prototyping before a proper driver is available, to access registers
that are unsupported by the proper driver or as the ultimate driver for any
device which is "simple enough".

It is assumed that the device allocates a continuous block of registers.
The registers should be independent, that means one device action is
performed by reading or writing only one register. Because each record
connects to one register, it is not possible to lock the device for several
consecutive register accesses with this device support, except if that task
is performed in the low level driver and hidden from _regDev_. The user has
to know the register offsets and data types when configuring the records.

The actual hardware access is neither defined nor implemented in this
device support. Instead, _regDev_ defines an interface for low level
drivers that implement the hardware access. One example of a low level
driver is _mmap_, a driver for memory mapped registers (using VME bus
access on systems that support it or `mmap()` on Linux).

What is defined in _regDev_ is the interface to the standard records. Thus
it is not necessary to deal with records when writing a new low level
driver. Consequently, all _regDev_ compatible devices look the same on the
record level. The general idea is to have a simple and flexible device
support interface which is independent of the underlying hardware
structure.

To be usable with _regDev_, a low level driver must implement a set of
[support functions](#driver-support-functions) and
[register](#registration) devices to _regDev_ by name. This name must be
unique among all _regDev_ devices on the same IOC. Typically, the low
level driver registers a device in a configuration function whis is called
from the IOC startup script.

In principle, a physical device can have many blocks of registers, e.g. on
differenct VME address spaces. The low level driver may handle this by
mapping different register blocks to different address offset ranges or
the low level driver registers each register block with an individual name.
In the latter case, _regDev_ will handle them as different, independent
devices.

A demo driver is included.

[TOC]

Device Support
--------------

_RegDev_ supports the standard record types
    [ai](#analog-input-ai),
    [ao](#analog-output-ao),
    [bi](#binary-input-bi),
    [bo](#binary-output-bo),
    [mbbi](#multi-bit-binary-input-mbbi),
    [mbbo](#multi-bit-binary-output-mbbo),
    [mbbiDirect](#multi-bit-binary-input-direct-mbbidirect),
    [mbboDirect](#multi-bit-binary-output-direct-mbbodirect),
    [longin](#integer-input-longin),
    [longout](#integer-output-longout),
    [stringin](#string-input-stringin),
    [stringout](#string-output-stringout),
    and [waveform](#waveform-input-waveform).
From EPICS R3.14.5 on
    [calcout](#calculation-output-calcout) is supported.
From EPICS R3.14.12 on
    [aai](#array-analog-input-aai) and
    [aao](#array-analog-output-aao) records are supported.
    (These records used to be commented out in earlier EPICS releases.)
From EPICS R3.15.0.2 on
    [lsi](#long-string-input-lsi) and [lso](#long-string-output-lso)
    records are supported.
From EPICS R3.16 on
    [int64in](#integer-64-input-int64in) and
    [int64out](#integer-64-output-int64out) are supported.

The `DTYP` is `"regDev"` for all record types, independent of the low level
driver.

If a record processes when the device is not connected (off, unreachable),
the record raises an alarm with `SEVR`=`"INVALID"` and `STAT`=`"READ"` or
`"WRITE"`. Not all low level drivers can necessarily detect a
disconnected state!

There is also a connection status support for [bi](#connection-status-bi).
The `DTYP` is `"regDev stat"`. This record does not raise an alarm when the
device is disconnected. It just changes to `0` when the device disconencts
and to `1` when it connects.

It is possible to use `I/O Intr` scanning if supported by the low level 
driver. Input records in `I/O Intr` mode process whenever new data has been
received from the device. This is the recommended `SCAN` mode for input
records if supported.

If supported by the low level driver, output records may be processed in
`I/O Intr` mode whenever the driver is ready to accept new output. This may
be useful for drivers with externally triggered or periodic output cycles.
But it is rarely supported or needed.

The general form of the `INP` or `OUT` link is:\
`"@devicename:offset:readbackoffset options"`

* `devicename` is the unique name that a low level driver has used to
  register the device.

* `offset` is the address offset of the register relative to the beginning
  of the register block of this device. It depends on the low level driver
  if offset is measured in bytes or anything else. The offset must be a
  positive integer expression within the limits of the device register
  block size. It may be calculated using the operators `+-*()`.

  It is possible to calculate `offset` dynamically from another record on
  the same IOC. The value of that record must be convertibe to `DBR_LONG`.
  In this case the name of the other record must be the the first operand
  in the calculation. If the record name contains any of the characters
  `:+-*()`, it must be single quoted.

  **Example:** `('other-record'-1)*8+0x100`

  If a dynamic offset exceed the limits of the register block at the time
  the record is processed, it will raise an alarm with `SEVR`=`"INVALID"`
  and `STAT`=`"READ"` or `"WRITE"`.
  If the referenced record cannot be read as a `DBR_LONG`, the record will
  raise an alarm with `SEVR`=`"INVALID"` and `STAT`=`"LINK"`.

* `readbackoffset` is optional. It is used by output records to initialize
  or update from a device register. If `readbackoffset` is not specified
  but the  second `:` is there, the output record reads from the normal
  `offset` address. If `:readbackoffset` is omitted (including the `:`),
  the record is not initialized by _regDev_ and may be initialized for
  example by auto save and restore. But it may still update from its normal
  `offset` address.
  For updating output records see option `U=period` below.

  Output records are initialized during `iocInit` in undefined order.

  If an output record is not initialized reading its value back from the
  register, it is suggested to initialize the register from the record
  using `PINI`=`"YES"` in order to avoid discrepancies between record and
  register.

  Dynamic offsets using another record is not supported for readback.

* `options` is a space separated list of `option=value` pairs to configure
  details of the record. Not all options are meaningful for every record
  type.
  All options have a default value which is used when the option is not
  specified. Options have a short (one letter) name and one or more long
  name alternatives for better readablility. Option names are not case
  sensitive.

  * `T=type` (long name: `type`) defines the data type of the register.

    Available choices:
    * `int8` = one byte signed integer
    * `uint8`, `char`, `byte` = one byte unsigned integer
    * `int16`, `short` = two bytes signed integer
    * `uint16`, `word` = two bytes unsigned integer
    * `int32`, `long` = four bytes signed integer
    * `uint32`, `dword` = four bytes unsigned integer
    * `int64`, `longlong` = eight bytes signed integer
    * `uint64`, `qword` = eight bytes unsigned integer
    * `float`, `float32`, `real32`, `single` = four bytes floating point
    * `double`, `float64`, `real64` = eight bytes floating point
    * `bcd8` = one byte unsigned binary coded decimal (two digits)
    * `bcd16` = two bytes unsigned binary coded decimal (four digits)
    * `bcd32` = four bytes unsigned binary coded decimal (eight digits)
    * `bcd64` = eight bytes unsigned binary coded decimal (sixteen digits)
    * `string` = byte string.
       There is no assumption about the character encoding, neither utf8
       nor any specific iso8859 encoding.

    The default is `T=int16` for most record types. For array records
    ([waveform](#waveform-input-waveform), [aai](#array-analog-input-aai)
    and [aao](#array-analog-output-aao)), the default is the type that
    matches the `FTVL` field.

  * `L=low` and `H=high` (long names: `lo` or `low` and `hi` or `high`)
    are used for linear conversion in [ai](#analog-input-ai) and
    [ao](#analog-outpu-ao) records if `LINR`=`"LINEAR"` and in
    [waveform](#waveform-input-waveform), [aai](#array-analog-input-aai)
    and [aao](#array-analog-output-aao) if `FTVL`=`"FLOAT"` or `"DOUBLE`"
    but `T` is an integer type.

    They define the raw values which correspond to `EGUL` and `EGUF` (or
    `LOPR` and `HOPR` for arrays) respectively. Output records will never
    write intger values lower than `L` or higher than `H`. Instead, the
    written output value saturates at the limit. That means instead of
    values beyond a limit, the limit value will be written. This prevents
    integer wrap-around.

    The default values for `L` and `H` depend on the data type.

    | T       |                   Default L                   |                  Default H                   |
    |:--------|----------------------------------------------:|---------------------------------------------:|
    | int8    |               -0x7f <br>                 -127 |               0x7f <br>                  127 |
    | uint8   |                0x00 <br>                    0 |               0xff <br>                  255 |
    | int16   |              -x7fff <br>               -32767 |             0x7fff <br>                32767 |
    | uint16  |              0x0000 <br>                    0 |             0xffff <br>                65535 |
    | int32   |         -0x7fffffff <br>          -2147483647 |         0x7fffffff <br>           2147483647 |
    | uint32  |          0x00000000 <br>                    0 |         0xffffffff <br>           4294967295 |
    | int64   | -0x7fffffffffffffff <br> -9223372036854775807 | 0x7fffffffffffffff <br>  9223372036854775807 |
    | uint64  |  0x0000000000000000 <br>                    0 | 0xffffffffffffffff <br> 18446744073709551615 |
    | bcd8    |                                            00 |                                           99 |
    | bcd16   |                                          0000 |                                         9999 |
    | bcd32   |                                      00000000 |                                     99999999 |
    | bcd64   |                              0000000000000000 |                             9999999999999999 |
    | float32 |                      n/a                      |                     n/a                      |
    | float64 |                      n/a                      |                     n/a                      |
    | string  |                                          40   |                     n/a                      |

    Note that for signed integer types, the default `L` is one off the
    smallest possible value. This makes linear conversion symmetric on the
    positive and negative side, that means it makes sure that 0 is in the
    center of the value range. As a side effect, the most negative value
    will never be written to the register but will be saturated at `L`. If
    that is a problem, set `L` explicitly and maybe adjust `LOPR`.

  * `L=length` (long names: `len` or `length`)
    For string records, `L` has a different meaning. It specifies
    the string length. Output records will write `length` bytes, either
    filling up with null bytes or trunctating and not terminating the
    string if necessary. Input records will read no more than `length`
    bytes and then terminate the string, which may overwrite the last byte
    read. For [stringin](#string-input-stringin) and
    [stringout](#string-output-stringout) records, the default length is
    the size of the `VAL` field (40). For array records with
    `FTVL`=`CHAR` or `UCHAR`, it is the array size `NELM`. For
    [lsi](#long-string-input-lsi) and [lso](#long-string-output-lso)
    records it is the value size `SIZV`.

  * `B=bit` (long name: `bit`) is only used for [bi](#binary-input-bi) and
    [bo](#binary-output-bo) records to define the bit number within the
    data byte, word, or doubleword, depending on `T`. Bit number 0 is the
    least significant bit. Note that in big endian byte order, also known
    as motorola format, bit 0 is in the last byte, while in little endian
    byte order, known as intel format, bit 0 is in the first byte. If in
    doubt and if supported by the hardware/low level driver, use `T=byte`
    to avoid any byte order problems when handling single bits.

  * `M=mask` (long name: `mask`) can be used to mask used bits. A `mask`
    value of `0` is ignored (i.e. no masking is performed). This is the
    default. If `mask` is not `0`, then any value read from a register is
    ANDed with the mask. When writing output, only the masked bits are
    modified. This usually causes a read access to the register followed
    by a write access. Some record types allow to set a mask with other
    means, usually the `MASK` field. In this case, both masks are applied,
    i.e. the effective mask is the `MASK` field AND the value of the
    `mask` option.

  * `I=invertmask` (long names: `inv` or `invert`) is used to invert bits
    before writing the value to or after reading it from a register. This
    allows to invert the logic of  bits. The default value is `0`, i.e. do
    not invert any bits. Can be used with all record types. Records that
    shift the register values also shift the invert mask. That means the
    inverted bits refer to the shifted value in the record, not directly
    to the bits in the register.

  * `P=packing` (long names: `packing` or `fifopacking`) is used for
    accessing arrays through FIFO registers. It defines how many array
    elements are packed in one register access. For example `T=int16 P=1`
    defines a FIFO of 16 bit values while `T=int16 P=2` is a FIFO
    with 32 bits width that contains two 16 bit values in each access.
    With `P` set, all array elements or packed groups of elements are
    read from the same register.

  * `F=feed` (long names: `feed`, `arrayfeed` or `interlace`) is used for
    arrays to specify an offset increment from one element to the next if
    that differs from the native element type size. It can be used to split
    interlaced arrays, i.e. a "table" with arrays in "columns", into
    separate records by specifying the "row" length in bytes. The feed is
    used instead of the native type size, not in addition to it. It can
    be negative, which effectively inverts the order of array elements.

  * `U=period` (long name: `update`) is used to update output records
    periodically. Every `period` milliseconds, the output record reads back
    the value from the register at `readbackoffset` or `offset` if
    `readbackoffset` is not specified, just like during initialization.
    Monitors on the record will register the change and the time stamp of
    the record will update, but the record will not really process. Thus,
    neither `FLNK` nor any other link will be followed.

    Instead of a period in milliseconds, the letter `T` can be used to
    trigger the update whenever a [bo](#update-trigger-bo) record with
    `DTYP`=`"regDev updater"` connected to the same device is processed.

    Updating is only supported for output records.

  * `V=vector` (long names: `vec`, `vector`, `ivec`, `irqvec`, `irq`,
    `intvec` or `interrupt`) is used together with `SCAN`=`I/O INTR` to
    bind the record to an interrupt vector. Whenever the interrupt with
    that `vector` number is received, the record is scheduled to process
    in a callback thread according to its `PRIO` field.

    Interrupts need support by the underlying low level driver. What
    exactly the vector number means depends on the low level driver.


Example Records
---------------

The following examples assume that the low level driver supports `I/O Intr`
mode for input registers. If that is not the case or not applicable for
all registers use any other scanning method instead.
All output records in the examples use `PINI`=`"YES"` to make sure an
initial value is written to the register. Depending on the low level
driver, this may not be necessary if the record gets initialized from a
register using `readbackoffset`.

### Connection Status (bi)

    record (bi, "$(RECORDNAME)") {
      field (DTYP, "regDev stat")
      field (INP,  "@$(DEVICE)")
      field (SCAN, "I/O Intr")
      field (ZNAM, "Disconnected")
      field (ONAM, "Connected")
    }

The record value is `1`=`"Connected"` if a connection to the device is
established and `0`=`"Disconnected"` if not.
Disconnect does not raise an alarm.

### Update Trigger (bo)

    record (bo, "$(RECORDNAME)") {
      field (DTYP, "regDev updater")
      field (OUT,  "@$(DEVICE)")
    }

Whenever the record processes with a non-zero value (i.e. true), all output
records connected to the same `$(DEVICE)` which have the option `U=T` set
in their `OUT` link update their values from the device using
`readbackoffset` if set, else the normal `offset`.

### Analog Input (ai)

The ai record can read integer or floating point registers.
Default type is `T=int16`. `T=string` is not valid for ai records.
Defaults for `L` and `H` depend on `T`, see table above.

#### Integer registers with linear conversion

    record (ai, "$(RECORDNAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T) L=$(L) H=$(H)")
      field (SCAN, "I/O Intr")
      field (LINR, "LINEAR")
      field (EGUL, "$(MINVAL)")
      field (EGUF, "$(MAXVAL)")
    }

If `T` is an integer type like `int16`, the register is copied into `RVAL`.
If `LINR`=`"LINEAR"`, then the record support converts `RVAL` to `VAL` so
that `L` maps to `EGUL` and `H` maps to `EGUF`.

If `T` does not fit in the range of the 32 bit signed `RVAL`, then the
device support just stores the lower 32 bits in `RVAL` but converts the
original value to `double`, applies smoothing and scaling as for floating
point values and stores the result in `VAL`, bypassing the normal
convertion by the record support, which cannot handle values outside the
32 bit signed integer range.

This can happen for `T=uint32`, `int64` or `uint64` if the value is greater
than `0x7ffffffff` or smaller than `-0x80000000`.

For 64 bit values, this conversion may lose the lowest bits due to the
limited precision of `double`. Those lowest bits are availible in `RVAL`
as long as no smoothing or scaling applies, but `RVAL` of course loses
the highest 32 bits.

#### Floating point registers

    record (ai, "$(RECORDNAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (SCAN, "I/O Intr")
    }

If `T` is a floating point type (`float` or `double`), the register value
is written directly into `VAL` and `EGUL` and `EGUF` are ignored. The
device support emulates adjustment scaling and smoothing according to
`ASLO`, `AOFF` and `SMOO` which is done by the record support itself only
when using conversion from integer:

`VAL = (register * ASLO + AOFF) * (1 - SMOO) + VAL_old * SMOO`

### Analog Output (ao)

The ao record can write integer or floating point registers.
Default type is `T=int16`. `T=string` is not valid for ao records.
Defaults for `L` and `H` depend on `T`, see table above.

#### Integer registers with linear conversion

    record (ao, "$(RECORDNAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T) L=$(L) H=$(H)")
      field (LINR, "LINEAR")
      field (PINI, "YES")
      field (EGUL, "$(MINVAL)")
      field (EGUF, "$(MAXVAL)")
    }

If `T` is an integer type, `RVAL` is written to the register. If
`LINR`=`"LINEAR"`, then the record support first scales the record value
(to be exact `OVAL`) so that `EGUL` maps to `L` and `EGUF` maps to `H`.
The record may then use adjustment scaling to modify `RVAL`. However, the
device support will never write any value lower than `L` or higher than
`H`. If necessary the value will be saturated at the limit, in order to
avoid integer wrap-around.

When initializing or updating an ao record, the same logic applies as for
reading an ai record, in particular bypassing the record support in case
the register value does not fit into `RVAL`.

#### Floating point registers

    record (ao, "$(RECORDNAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (PINI, "YES")
    }

If `T` is a floating point type (`float` or `double`), `OVAL` is written to
the register and `EGUL` and `EGUF` are ignored. The device support emulates
adjustment scaling according to `AOFF` and `ASLO` which is only done by the
record support when converting to integer:

`register = (OVAL - AOFF) / ASLO`

### Calculation Output (calcout)

    record(calcout, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T) L=$(L) H=$(H)")
      field (PINI, "YES")
    }

Default type is `T=int16`. Defaults for `L` and `H` depend on `T`, see
table above.

`OVAL` (the result of `CALC` or `OCAL`, depending on `DOPT`) is written to
the register. If `T` is an integer type, the value is truncated to an
integer and compared to `L` and `H`. If `OVAL` is lower than `L` or higher
than `H`, it will be saturated at the limit.

If `T=float` or `T=double`, `OVAL` is written to the register directly
without any conversion.

`T=string` is not valid for calcout records.

EPICS release R3.14.5 or higher is required to use device support with
calcout records.

### Binary Input (bi)

    record(bi, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T) B=$(B)")
      field (SCAN, "I/O Intr")
    }

Default type is `T=int16`. Default bit is `B=0`

Depending on `T`, `B` can vary from `0` to `7`, `15`, or `31`. Bit 0 is the
least significant bit. In little endian byte order, bit 0 is in the first
byte, in big endian byte order it is in the last byte of the register.
If in doubt and if supported by the hardware, use `T=byte` to avoid any
byte order problems when handling single bits.

In the value read from the register and masked with the `MASK` field,
which defaults to `1<<B`. The result is written to `RVAL`.
The record then sets `VAL` to `0` if `RVAL` is `0` or to `1` otherwise.

`RVAL = register & MASK; VAL = RVAL ? 1 : 0`

`T=string`, `T=float` or `T=double` are not valid for bo records.
Signed and unsigned types are equivalent.

### Binary Output (bo)

    record(bo, "$(NAME)") {
       field (DTYP, "regDev")
       field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T) B=$(B)")
       field (PINI, "YES")
    }

Default type is `T=int16`. Default bit is `B=0`.

Depending on `T`, `B` can vary from `0` to `7`, `15`, or `31`. Bit 0 is the
least significant bit. In little endian byte order, bit 0 is in the first
byte, in big endian byte order it is in the last byte of the register.
If in doubt and if supported by the hardware, use `T=byte` to avoid any
byte order problems when handling single bits.

If `VAL` is not `0`, then `RVAL` is set to the `MASK` field, which defaults
to `1<<B`, else `RVAL` is set to `0`. Only the masked bits of the register
are modified while all other bits remain unchanged. Thus, other output
records can write to different bits of the same register. This may cause
two register accesses, one for reading the original value and another one
to write back the result.

`RVAL = VAL ? MASK : 0; register = (register_old & ~MASK) | RVAL`

`T=string`, `T=float` or `T=double` are not valid for bo records.
Signed and unsigned types are equivalent.

### Multi Bit Binary Input (mbbi)

    record(mbbi, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (SCAN, "I/O Intr")
      field (NOBT, "$(NUMBER_OF_BITS)")
      field (SHFT, "$(RIGHT_SHIFT)")
    }

Default type is `T=int16`.

The register is read, masked with `NOBT` bits (shifted by `SHFT` bits) and
written to `RVAL`. The record support then shifts the value right by `SHFT`
bits, compares it to the `*VL` fields and writes the index of the found
match to `VAL`.

Valid values for `NOBT` and `SHFT` depend on `T`:
`NOBT+SHFT` must not exceed the number of bits of the type.

Bit 0 is the least significant bit. In little endian byte order, bit 0 is
in the first byte, in big endian byte order it is in the last byte of the
register.

`T=string`, `T=float` or `T=double` are not valid for mbbi records.
Signed and unsigned types are equivalent.

### Multi Bit Binary Output (mbbo)

    record(mbbo, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (PINI, "YES")
      field (NOBT, "$(NUMBER_OF_BITS)")
      field (SHFT, "$(LEFT_SHIFT)")
    }

Default type is `T=int16`.

The record support uses `VAL` as an index to select a `*VL` value, shifts
it left by `SHFT` bits and writes it to `RVAL`. That value is then masked
with `NOBT` bits (also shifted) and written to the register.

Only the referenced `NOBT` bits of the register are modified. All other
bits remain unchanged. Thus, other output records can write to different
bits of the same register.

Valid values for `NOBT` and `SHFT` depend on `T`:
`NOBT+SHFT` must not exceed the number of bits of the type.

Bit 0 is the least significant bit. In little endian byte order, bit 0 is
in the first byte, in big endian byte order it is in the last byte of the
register.

`T=string`, `T=float` or `T=double` are not valid for mbbo records.
Signed and unsigned types are equivalent.

### Multi Bit Binary Input Direct (mbbiDirect)

    record(mbbiDirect, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (SCAN, "I/O Intr")
      field (NOBT, "$(NUMBER_OF_BITS)")
      field (SHFT, "$(RIGHT_SHIFT)")
    }

Default type is `T=int16`.

The register is read, masked with `NOBT` bits (shifted by `SHFT` bits) and
written to `RVAL`. The record support then shifts the value right by `SHFT`
bits, writes it to `VAL` and sets the `B*` fields according to the bits in
`VAL`.

Valid values for `NOBT` and `SHFT` depend on `T`:
`NOBT+SHFT` must not exceed the number of bits of the type.

Bit 0 is the least significant bit. In little endian byte order, bit 0 is
in the first byte, in big endian byte order it is in the last byte of the
register.

`T=string`, `T=float` or `T=double` are not valid for mbbiDirect records.
Signed and unsigned types are equivalent.

### Multi Bit Binary Output Direct (mbboDirect)

    record(mbboDirect, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (PINI, "YES")
      field (NOBT, "$(NUMBER_OF_BITS)")
      field (SHFT, "$(LEFT_SHIFT)")
    }

Default type is `T=int16`.

The record support shifts `VAL` left by `SHFT` bits and writes it to
`RVAL`. That value is then masked with `NOBT` bits (also shifted) and
written to the register.

Only the referenced `NOBT` bits of the register are modified. All other
bits remain unchanged. Thus, other output records can write to different
bits of the same register.

Valid values for `NOBT` and `SHFT` depend on `T`:
`NOBT+SHFT` must not exceed the number of bits of the type.

Bit 0 is the least significant bit. In little endian byte order, bit 0 is
in the first byte, in big endian byte order it is in the last byte of the
register.

`T=string`, `T=float` or `T=double` are not valid for mbboDirect records.
Signed and unsigned types are equivalent.

### Integer Input (longin)

    record(longin, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (SCAN, "I/O Intr")
    }

Default type is `T=int16`.

The register is read and the value is written to `VAL`. Depending on `T`,
the value is zero extended or sign extended to 32 bits.

`T=string`, `T=float` or `T=double` are not valid for longin records.

### Integer Output (longout)

    record(longout, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (PINI, "YES")
    }

Default type is `T=int16`.

Depending on `T`, the least significant 8, 16, or 32 bits of `VAL` are
written to the register.

`T=string`, `T=float` or `T=double` are not valid for longout records.

### Integer 64 Input (int64in)

    record(int64in, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (SCAN, "I/O Intr")
    }

Default type is `T=int64`.

The register is read and the value is written to `VAL`. Depending on `T`,
the value is zero extended or sign extended to 64 bits.

`T=string`, `T=float` or `T=double` are not valid for int64in records.

EPICS release R3.16 or higher is required to use int64in records.

### Integer 64 Output (int64out)

    record(int64out, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) T=$(T)")
      field (PINI, "YES")
    }

Default type is `T=int64`.

Depending on `T`, the least significant 8, 16, 32, or 64 bits of `VAL` are
written to the register.

`T=string`, `T=float` or `T=double` are not valid for int64out records.

EPICS release R3.16 or higher is required to use int64out records.

### String Input (stringin)

    record(stringin, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET) L=$(LENGTH)")
      field (SCAN, "I/O Intr")
    }

Default and only valid type is `T=string`. Default length is `L=40`.

`L` bytes are copied from the register to `VAL`. The string is then
null-terminated, which may delete byte 39.

### String Output (stringout)

    record(stringout, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET) L=$(LENGTH)")
      field (PINI, "YES")
    }

Default and only valid type is `T=string`. Default length is `L=40`.

`L` bytes are copied  from `VAL` to the register. If the actual string
length of `VAL` is shorter than `L`, the remaining space is filled with
null bytes. If it is longer than `L`, the string is truncated and not
null-terminated.

### Long String Input (lsi)

    record(lsi, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET)")
      field (SIZV, "$(LENGTH)")
      field (SCAN, "I/O Intr")
    }

Default and only valid type is `T=string`. Default length is `L=SIZV`.

`L` bytes are copied from the register to `VAL`. The string is then
null-terminated, which may delete the last byte.

EPICS release R3.15.0.2 or higher is required to use lsi records.

### Long String Output (lso)

    record(stringout, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET)")
      field (SIZV, "$(LENGTH)")
      field (PINI, "YES")
    }

Default and only valid type is `T=string`. Default length is `L=SIZV`.

`L` bytes are copied  from `VAL` to the register. If the actual string
length of `VAL` is shorter than `L`, the remaining space is filled with
null bytes. If it is longer than `L`, the string is truncated and not
null-terminated.

EPICS release R3.15.0.2 or higher is required to use lso records.

### Waveform Input (waveform)

    record(waveform, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET)")
      field (SCAN, "I/O Intr")
      field (NELM, "$(NUMBER_OF_ELEMENTS)")
      field (FTVL, "$(FIELDTYPE)")
    }

`NELM` elements are read from registers and stored in `VAL`.

The default type `T` depends on `FTVL`. For example `FTVL=LONG` results in
`T=INT32`. But it is possible to change the type to enforce conversion:
If `T` is an integer type, e.g. `T=INT32` but `FTVL` is either `FLOAT` or
`DOUBLE`, then values are scaled so that `L` maps to `LOPR` and `H` maps
to `HOPR`. Otherwise, `T` and `FTVL` must match, at least when ignoring
signedness.

If `T=string` then `FTVL` must be `CHAR` or `UCHAR` or `STRING`.
For `CHAR` and `UCHAR`, `L=length` can be specified but defaults to
`NELM` and should not exceed it. That many characters are copied from
registers to `VAL`. If `L` is less than `NELM`, the remaining elements are
left unchanged. If `FTVL` is `STRING` then `NELM` strings, each of length
`L` (default 40), are copied to `VAL`.

### Array Analog Input (aai)

    record(aai, "$(NAME)") {
      field (DTYP, "regDev")
      field (INP,  "@$(DEVICE):$(OFFSET)")
      field (SCAN, "I/O Intr")
      field (NELM, "$(NUMBER_OF_ELEMENTS)")
      field (FTVL, "$(FIELDTYPE)")
    }

`NELM` elements are read from registers and stored in `VAL`.

The default type `T` depends on `FTVL`. For example `FTVL=LONG` results in
`T=INT32`. But it is possible to change the type to enforce conversion:
If `T` is an integer type, e.g. `T=INT32` but `FTVL` is either `FLOAT` or
`DOUBLE`, then values are scaled so that `L` maps to `LOPR` and `H` maps
to `HOPR`. Otherwise, `T` and `FTVL` must match, at least when ignoring
signedness.

If `T=string` then `FTVL` must be `CHAR` or `UCHAR` or `STRING`.
For `CHAR` and `UCHAR`, `L=length` can be specified but defaults to
`NELM` and should not exceed it. That many characters are copied from
registers to `VAL`. If `L` is less than `NELM`, the remaining elements are
left unchanged. If `FTVL` is `STRING` then `NELM` strings, each of length
`L` (default 40), are copied to `VAL`.

The aai record is similar to the waveform record, but may be more
efficient, because the low level driver may use DMA to fill the record.
Aai records must be enabled in EPICS base, which is not the default
before EPICS release R3.14.12.

### Array Analog Output (aao)

    record(aai, "$(NAME)") {
      field (DTYP, "regDev")
      field (OUT,  "@$(DEVICE):$(OFFSET)")
      field (NELM, "$(NUMBER_OF_ELEMENTS)")
      field (FTVL, "$(FIELDTYPE)")
      field (PINI, "YES")
    }

`NELM` elements of `VAL` are written to registers.

The default type `T` depends on `FTVL`. For example `FTVL=LONG` results in
`T=INT32`. But it is possible to change the type to enforce conversion:
If `T` is an integer type, e.g. `T=INT32` but `FTVL` is either `FLOAT` or
`DOUBLE`, then values are scaled so that `LOPR` maps to `L` and `HOPR` maps
to `H`.  Otherwise, `T` and `FTVL` must match, at least when ignoring
signedness.

The device support will never write any value lower than `L` or higher than
`H`. If necessary. the value will be saturated at the limit.

If `T=string` then `FTVL` must be `CHAR` or `UCHAR` or `STRING`.
For `CHAR` and `UCHAR`, `L=length` can be specified but defaults to
`NELM` and should not exceed it. That many characters are copied from
`VAL` to registers. If `L` is less than `NELM`, the remaining elements are
ignored. If `FTVL` is `STRING` then `NELM` strings, each of length `L`
(default 40), are copied to registers.

The low level driver may use DMA to write the array. Aai records must be
enabled in EPICS base, which is not the default before EPICS release
R3.14.12.


Block Mode
----------

To improve data exchange efficiency, _regDev_ can work in "block mode".
In this mode, not each record reads from or writes to the device registers
directly but instead the whole device address space is transfered to or
from RAM and records interact with this copy only. This may also allow for
efficient DMA transfers.

Transfer of the memory block is triggered by processing a connected record
with `PRIO="HIGH"`. If the record is an input record, the block is read
from the device as an array of the data type used by the triggering record.
After that, all connected `I/O Intr` input records are processed to read
their data from the copy in memory.

If the record is an output record, the block is written to the device as an
array of the data type used by the triggering record. After that, all
connected `I/O Intr` output records are processed to write their data to
the copy in memory, ready to be written to the device the next time the
transfer is triggered.

In block mode, [aai](#array-analog-input-aai) and
[aao](#array-analog-output-aao) records may be mapped directly into the
block buffer. This avoids copying data between block buffer and record.
Mapping is only possible if the data does not need to be modified by
swapping, scaling, masking, inverting, packing or interlacing. If using
EPICS releases before R3.15.1, the offset must be constant.


Driver Functions
----------------

### Driver Support Functions

A low level driver must implement the functions it wants to support and
fill the function pointers into a `regDevSupport` structure. It contains
the functions `report`, `getInScanPvt`, `getOutScanPvt`, `read`, and
`write`. Use `NULL` for any API function that is not implemented by the
driver. The support structure can be global and static like this:

    static regDevSupport support = {
        report,
        getInScanPvt,
        getOutScanPvt,
        read,
        write
    };

The functions shall either be `static` or use names with a driver specific
prefix, so that different low level driver implementations for _regDev_
can exist in the same IOC.

The driver can assume that all support functions are called in a thread
safe context. That means it will never happen that two support functions
are called for the same device at the same time. However, support functions
for different devices may be called at the same time, even if using the
same low level driver.


    void report (regDevice* device, int level);
    
This function is called by `dbior` and shall print device information to
`stdout`. _regDev_ has already printed the name and (if known) the size of
the device. If the device is working in block mode, the block buffer
address has been printed, too. The `report` function may print additional
information with a detail level defined by the level passed to `dbior`.
After calling the `report` function, but not before, _regDev_ prints a
newline.

    
    IOSCANPVT getInScanPvt (regDevice* device, sizet offset, unsigned int vec, const char *user);
    IOSCANPVT getOutScanPvt (regDevice* device, sizet offset, unsigned int vec, const char *user);

These two function provide support for `I/O Intr` scanning for input and
output records. The driver shall implement `getInScanPvt` if the device
has asynchonous input signalling, e.g. by interrupts.

Rarely a driver implements `getOutScanPvt` but can do so to process
`I/O Intr` scanned output records directly after data has been written to
the device in some asynchronous way so that output data can be updated for
the the next time data is written to the device.

Be aware that in [block mode](#block-mode) `I/O Intr` scanning has a
different meaning for records that do not trigger block transfers. In this
mode, only records with `PRIO="HIGH"` use the `I/O Intr` scanning provided
by these functions. Other `I/O Intr` records are scanned after the block
transfer has finished, even if the driver does not provide any of these
two functions.


    int read (regDevice* device, sizet offset, unsigned int datalength, sizet nelem, void* pdata, int priority, regDevTransferComplete callback, const char* user);
    int write (regDevice* device, sizet offset, unsigned int datalength, sizet nelem, void* pdata, void* pmask, int priority, regDevTransferComplete callback, const char* user);

These two functions are the heart of the driver support. Usually drivers
implement both to do the actual I/O. Some drivers may prefer to use DMA to
transfer larger pieces of data e.g. with `nelem` much greater than 1.

* `offset` is the address offset of the register relative to the
  beginning to the address space of this device.
* `datalength` is the length of the register in bytes (one element in case
  of arrays).
* `nelem` is the number of elements in an array. It is `1` for scalar
  values but may be larger for arrays or strings. It may also be `0`. In
  that case the driver shall just report the connection state by returning
  `SUCCESS` or an error code.
* `pdata` is a pointer to a buffer of `nelem * datalength` bytes. The low
  level driver shall copy data from device registers to this buffer or from
  this buffer to device registers.
  It may use the [API function](#api-functions) `regDevCopy` to copy the
  data and make sure `datalength` is respected for register access.
  If `nelem==0`, `pdata` may be `NULL` and no data shall be transfered.
* `pmask` is a pointer to a bit mask of `datalength` bytes or `NULL`. If
  not `NULL`, only those bits set in the mask shall be modified in the
  register. All other bits shall remain unchanged. This usually requires
  read-modify-write access to the register.
* `priority` is a number from `0` to `2` and may be used as a hint for the
  driver to schedule requests. `0` is the lowest and `2` the highest
  piority. It is taken from the `PRIO` field of the record.
* `callback` is a function to be called upon request completion if the
  driver decides to handle the request asynchronously, e.g. as DMA, and
  returns `ASYNC_COMPLETION`. The `callback` function takes two arguments:
  The `const char* user` pointer passed to `read` or `write` and an
  `int status` which may either be `SUCCESS` or an error code.
  The driver may ignore `callback` completely and handle all requests
  synchronously. The `callback` function pointer may be `NULL`. In that
  case the driver must handle the request synchronously (and is allowed to
  block doing so) and must not return `ASYNC_COMPLETION`.
* `user` is a string which can be used for debug and error messages. It is
  the record name (actually a pointer to the record itself). If the driver
  decides to use the `callback`, this pointer must be passed.

Strings are handled as arrays of characters: `datalength` is 1 and `nelem`
is the buffer size including space for any terminating null byte.

### Registration

Each device must be registered with the following function:

    int regDevRegisterDevice (const char* devicename, const regDevSupport* support, regDevice* device, size_t size);

The `devicename` must be unique on the IOC and is used in the record links
to reference the device. The `support` parameter is a pointer to the
`regDevSupport` [structure](driver-support-functions) of this driver. The
parameter `device` is a pointer to a driver private `regDevice` structure
instance for this device. It is used as an opaque handle for the device by
_regDev_. That means _regDev_ itself does not access its contents but it
passes it to all support functions it calls. The driver can freely
`typedef struct regDevice` to its own needs and put in any information it
needs to operate one registered device. The driver shall allow to register
many independent devices with different names.
If the `size` parameter is greater than `0`, _regDev_ will check the addess
offsets and data size of records against this limit and will never call
`read` or `write` with address ranges exceeding it. If `size` is `0`, it is
assumed to be unkown at the time of registration. In that case the low
level driver is resposible for catching `read` or `write` beyond any
run-time limits.

### API Functions

The low level driver can use several functions provided by _regDev_ during
its initialization and in its support functions.


    regDevice* regDevFind(const char* name);
    const char* regDevName(regDevice* device);

These functions convert a device name to a device handle or vice versa.
The device must have been registered. On failure, `NULL` is returned.


    int regDevLock(regDevice* device);
    int regDevUnlock(regDevice* device);

These functions lock and unlock access to a device using a device specifc
mutex. When support functions are called, the device is already locked, but
in asynchonous functions of a driver, for example in threads started by the
driver, locking the device explicitly may be necessary.


    int regDevInstallWorkQueue(regDevice* device, unsigned int maxEntries);

A low level driver may call this function in its initialization routine to
offload all asynchonous handling to _regDev_. That means all support
functions will be serialized and called from a device specific thread
created by _regDev_ with `callback=NULL`. Useful if the device driver does
not need to do anything special like interrupt handling asynchonusly but
still needs to be able to block in its support functions 'read' or 'write'.
The parameter `maxEntries` defines the size of the work queue for this
device. Queueing more records than `maxEntries` will fail and the rejected
records will raise an alarm with `SEVR`=`"INVALID"` and `STAT`=`"SOFT"`.


    int regDevRegisterDmaAlloc(regDevice* device, void* (*dmaAlloc) (regDevice *device, void* ptr, size_t size));

This function registers a DMA memory allocator that will be used by
_regDev_ to allocate memory for [aai](#array-analog-input-aai) and
[aao](#array-analog-output-aao) records as well as for devices using
[block mode](#block-mode). This allows a low level driver to provide DMA
enabled memory for for arrays and block devices and then to use DMA in
its `read` and `write` support functions.
If no DMA allocator is registred, _regDev_ will simply use `malloc`.
If the `ptr` parameter is not `NULL`, the device shall free that memory
and allocate new memory of `size` bytes. If `size` is `0`, the device shall
simply free `ptr` and return `NULL`. This is similar to `realloc` but
the device does not need to copy any content from the old to the new
buffer.


    int regDevMakeBlockdevice(regDevice* device, unsigned int modes, int swap, void* buffer);

Calling this function during initialization after registering the device
turns it into [block mode](#block-mode). The low level driver may
provide a suitable (e.g. DMA enabled) `buffer` for the data block. If
`buffer` is `NULL`, _regDev_ will call the registered `dmaAlloc` function
to allocate memory in the size of the device or call `malloc` if no
`dmaAlloc` function has been registered. The `modes` parameter can be
`REGDEV_BLOCK_READ`, `REGDEV_BLOCK_WRITE` or the combination
`REGDEV_BLOCK_READ|REGDEV_BLOCK_WRITE` to define if block mode shall be
used for reading, writing or both. The `swap` parameter may be used to
tell _regDev_ to swap the byte order of the data after reading or before
writing and may be `REGDEV_NO_SWAP`, `REGDEV_DO_SWAP`, `REGDEV_BE_SWAP`
or `REGDEV_LE_SWAP` to swap byte order never, always, only on big endian
cpus or only on little endian cpus, respectively. 


    void regDevCopy(unsigned int datalength, size_t nelem, const volatile void* src, volatile void* dest, const void* pmask, int swap);

This helper function can be used usually by the 'read' or 'write' support
functions to copy data between memory mapped registers and RAM or between
two buffers. It copies `nelem` elements `datalength` bytes wise.
For `datalength` values of 1, 2, 4, or 8 it uses the proper access data
sizes to make sure that hardware sees correct access sizes. For other
values (and in case 8 byte access is not supported) is splits the access
into smaller chunks but never accesses across the border of one element.
If `pmask` is not `NULL`, it is a pointer to a bitmask of `datalength`
bytes size. In this case only bits in that bitmask are modified. That means
for each element, the `dest` value is first read, only the bits defined in
`pmask` are copied over from `src` and then the result is written back to
`dest`. If `swap` is `REGDEV_DO_SWAP`, each elements is swapped
`datalength` bytes wise from `src` to `dest` (`pmask` is assumed to be in
the byte order of `src`). If `swap` is `REGDEV_BE_SWAP` or
`REGDEV_LE_SWAP`, swapping is only performed on big endian or on little
endian cpus, respectively. If `swap` is `REGDEV_NO_SWAP`, data is copied
without swapping.


Debugging
---------

The global variable `regDevDebug` can be set in the startup script or at
any time on the command line to change the amount or debug output.
The following levels are supported:

| level| meaning                     |
|-----:|:----------------------------|
|   -1 |   fatal errors only         |
|    0 |   errors only (default)     |
|    1 |   startup messages          |
|    2 | + output record processing  |
|    3 | + input record processing   |
|    4 | + driver calls              |
|    5 | + io printout               |

Be careful using `level>1` because many messages can introduce
considerable delays which may result in different timing behavior than
in normal operation and may even lead to connection losses.

On vxWorks, `regDevDebug` can be set with `regDevDebug=level`.

In the iocsh use `var regDevDebug level`.

### Record debugging

To debug individual records, the `TPRO` field can be set.
A value of 1 enables basic debugging, a value of 2 or higher
also prints the the io data of the record.

---

Dirk Zimoch dirk.zimoch@psi.ch, 2009-2024
