# MoarVM Heap Snapshot Format

This article describes the Heap Snapshot file format for `.mvmheap` files, as taken by MoarVM (https://github.com/moarvm/moarvm).
The heap snapshot format has had two earlier versions already. This article only describes version 3.

## Subversioning

There is a "file metadata" piece in mvmheap files that points out which "subversion" of the format is present in the file in question.
So far there has only been subversion 1.
The very beginning of the file is 16 bytes of identification. Those bytes are:
`4d6f 6172 4865 6170 4475 6d70 7630 3033`, or in human-readable: `MoarHeapDumpv003`.

## Basic building blocks

The Format is comprised of blocks. They start with a "kind" name, which is 8 bytes of ascii letters describing very roughly what's in the block itself.
Here is the different kinds as they exist at the moment:

### ZSTD compressed integer columns

These blocks follow their kindname with two bytes of "entry size" and 8 bytes of "size", which in the case of ZSTD compressed blocks is optional and may be 0. In that case, read ZSTD data until the decompressor indicates that the frame has finished.
Inside the ZSTD compressed data there will just be number after number at the given entry size. Currently entry sizes of 2, 4, and 8 occur.

* collectables
    * **colkind** For the different kinds that exist, see further below
    * **colsize** managed size
    * **colusize** unmanaged size
    * **coltofi** `type_or_frame_index`, indexes into *types* and *frames* columns
    * **colrfcnt** outgoing reference count of a col
    * **colrfstr** start index of references in *references*
* references
    * **refdescr** description of reference (string)
    * **reftrget** target of reference
* types
    * **reprname** name of repr (string)
    * **typename** name of type (string)
* frames
    * **sfname** name of frame (string)
    * **sfcuid** cuid of frame (string)
    * **sfline** line of frame
    * **sffile** filename of frame (string)
* highscores
    * **topIDs** collectable ID of entry
    * **topscore** score of entry

These represent essentially one column of a set of "tables".

#### colkind values

1. KIND_OBJECT
2. TYPE_OBJECT
3. KIND_STABLE
4. KIND_FRAME
5. KIND_PERM_ROOTS
6. KIND_INSTANCE_ROOTS
7. KIND_CSTACK_ROOTS
8. KIND_THREAD_ROOTS
9. KIND_ROOT
10. KIND_INTERGEN_ROOTS
11. KIND_CALLSTACK_ROOTS

### ZSTD compressed miscellaneous blocks

#### string heap

The string heap in a heap snapshot is a zstd compressed block with a 32bit integer encoding a string's length followed by the string as the heap snapshot profiler keeps it - currently that is just utf8; there is not yet any support for strings with internal null bytes or strings that are invalid utf8. It would be easy to just switch that to utf8-c8, though.

### TOC blocks

A block of kind **toc** starts with a 64 bit integer containing the number of toc entries contained within. After that come triplets of a `kindname` (8 bytes ascii), the start position in the file as a 64bit integer, and the end position in the file as a 64bit integer.
The end of the TOC block has the start position of this toc block (where the initial "toc" kind name lives, right before the number of entries) so that when opening a file, you can seek to the very end, read one 64bit integer, and find a TOC.

### non-compressed blocks

#### filemeta

A regular old block: 8 bytes of type name ("filemeta"), the size of the block as a 64bit integer, then a blob of JSON data, followed by a single terminating null byte.

##### Structure

```JSON
{
  "subversion": 1,
  "start_time": 625256000000,
  "highscore_structure": {
    "entry_count": 40,
    "data_order": [
      "types_by_count", "frames_by_count",
      "types_by_size", "frames_by_size"
    ]
  }
```
The highscore structure tells how to interpret the numbers in `topIDs` and `topscore`. In this example there will be 4 times 40 numbers in each of the two. The first 40 are for types, then 40 for frames, then 40 for types and another 40 for frames. The first 80 scores are in "count", the second 80 scores are in "size" "units".

#### snapmeta

Same format as the filemeta, but occurs once per snapshot in the file.

##### Structure

```JSON
{
  "snap_time": 625256000000,
  "gc_seq_num": 3,
  "total_heap_size": 1234567,
  "total_objects": 9999,
  "total_typeobjects": 100,
  "total_stables": 101,
  "total_frames": 99999,
  "total_refs": 987654
}
```

`snap_time` is the time this individual snapshot got taken, `gc_seq_num` is the GC sequence number belonging to the snapshot, the rest of the fields are summary counts.

## Superstructure

At the very end of the file, the "outer" TOC can be found. Since it ends in a 64bit integer pointing at the start of the TOC, seeking to the end allows a parser to discover all contents of a given file.
The "outer" TOC will have the `filemeta` in it and then a bunch of "inner" TOCs. Each of these inner TOCs normally corresponds to one snapshot.
Each snapshot's TOC will point at blocks of basically all kinds. There is also a `strings` block for every snapshot, but it is allowed to be missing. This is the case because snapshots are written to the file as they are taken, and consecutive snapshots often need a few more strings. Only these additional strings are contained in each of the later `strings` blocks.
The same goes for the tables *frames* and *types*. Indices from other tables refer to the concatenation of all of these blocks.
In the case where the end of the file doesn't properly point at a TOC (for example if the program crashed while writing the snapshot), a file can also be read from the beginning. ZSTD will point out whenever a given compressed blob ends, and uncompressed blocks mandatorily have their size at the beginning.
