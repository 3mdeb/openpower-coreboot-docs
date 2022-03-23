Hostboot stores information in a tree made of targets and attributes associated
with them. Below is a short overview.

# APIs

* `FAPI_ATTR_*()` macros.
* Methods of `TARGETING::Target` like `getAttr()`, `tryGetAttr()`.
* There might be others.

# Data sources

## Hostboot's code

There are methods for setting attributes and Hostboot uses them, so potentially
any value can be modified.

## talos.xml

Machine specification like
[talos.xml](https://git.raptorcs.com/git/talos-xml/plain/talos.xml)
contains most of the attributes or at least their default values.

Mind that defaults can be specified at multiple levels and default closest to
the target in question has the highest priority.

## Processed talos.xml

At some point during the build of Hostboot machine specification is processed by
Perl scripts which reorganize it and fill in values of some attributes (other
things are removed, so resulting content isn't a superset of `talos.xml`).

Command for generating it:

```
$ cd talos-hostboot/src/usr/targeting/common
$ perl -I. processMrw.pl -x ~/data/talos.xml -o talos-mrw.xml
$ vim talos-mrw.xml
```

Some of the generated attributes: `ATTR_IPMI_SENSORS`.

## Attribute-specific get/set methods

At least `src/include/usr/fapi2/attribute_service.H` contains functions which
are called on use of `FAPI_ATTR_*()` macros for certain attributes. So if you
don't see where attribute's value is set or where its value is used, it might be
generated/consumed by one of those functions.
