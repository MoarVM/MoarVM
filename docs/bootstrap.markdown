# Bootstrap
MoarVM is based around the 6model object system. This must be bootstrapped as
pretty much the first thing that the VM does after startup.

## Bootstrap Procedure
The bootstrap is done something like this.

* Create a type object that will become BOOTStr, the VM's core string type.
  It will have no meta-object yet, and the STable will not be filled out. It
  will use the MVMString representation.
* Populate the representations table and name to ID hash. This includes setting
  up all of the representation function tables. We needed the BOOTStr first, as
  representation function tables contain the representation name in string form.
* Create a type object BOOTArray, the VM's core array type. It will have the
  MVMObjectArray representation. Again, there's no meta-object just yet.
* Create a type object BOOTHash, the VM's core hash type. It will have the
  MVMHash representation. Still no meta-objects.
* Create a type object BOOTCCode, the VM's core code type for things implemented
  inside them VM in C (typically, just a very small number of bootstrap things).
  It will have the MVMCFunction representation. Still no...yeah, you got it. :-)
* At this point, we finally have enough to bootstrap KnowHOW, the most primitive
  object type. This involves the KnowHOWREPR representation.
* Finally, the various BOOT type objects get meta-objects pieced together,
  which are KnowHOWs. Note that it almost certainly doesn't offer any real
  functionality; the point is just to get a clean bootstrap with nothing
  left dangling.

Beyond that, there's nothing left to do in the VM core; all other objects are
set up from code running atop of the VM.
