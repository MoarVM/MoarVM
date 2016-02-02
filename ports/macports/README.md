When updating the `Portfile` to reflect the new release:

1. Setup a [local portfile directory](https://guide.macports.org/chunked/development.local-repositories.html)
    * Checkout the latest version from [SVN](http://trac.macports.org/browser/trunk/dports/lang/MoarVM) (or run `svn up` if you have done that already to make sure that you start with the latest version from the MacPorts repository).

            export MYDIR=/your/path
            cd $MYDIR
            svn co --depth=empty https://svn.macports.org/repository/macports/trunk/dports macports && cd macports
            svn up --depth=emtpy lang && cd lang
            svn up MoarVM nqp rakudo

    * Run `portindex`, so that MacPorts will be able to find the port:

            cd $MYDIR/macports && portindex

    * Edit `/opt/local/etc/macports/sources.conf`, adding a reference to a local directory, like: `file:///your/path/macports/`

    * Verify that `port dir MoarVM` returns `/your/path/macports/lang/MoarVM`.

2. Edit the `Portfile`:
    * Update the `version` field to the latest version.
    * Remove the `revision` (if any revision is present).
    * Save.
    * Run `sudo port -v checksum MoarVM` from the shell.
      This will generate a bunch of output, including replacement checksum lines;
      copy them back into the `Portfile`, replacing the original versions. Save.
      (If you generate the tarballs yourself, you might want to double-check the checksums with `openssl dgst -sha256 <your-original-file>` or with any similar technique.)

3. Test the changes:

        port info MoarVM # should give you the latest version
        sudo port -v -t test MoarVM
        sudo port -v -t install MoarVM

4. Open a ticket to update the portfile
    * Create a unified diff with

            cd $(port dir MoarVM)
            svn diff > /tmp/MoarVM-x.y.Portfile.diff

    * Open a new ticket on http://trac.macports.org/ (you'll need a trac account first).
        * **summary**: `MoarVM: update to version x.y`
        * **version**: `(none)`
        * **type**: update
        * **CC**: add (co)maintainers (you get full emails with `port info MoarVM`) and potentially other developers that might be interested
        * **keywoords**: `haspatch` (or `haspatch maintainer` if you are also listed as maintainer)
        * **port**: `MoarVM`
        * Make sure to attach the diff.

    * Hang out in `#macports` on freenode and mention the ticket or write to the macports-dev mailing list with URL to the ticket and a descriptive subject if there is no response for a while.

5. Push the updated `Portfile` also to git.
