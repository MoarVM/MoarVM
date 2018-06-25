#!/usr/bin/env bash
# Script which generates documentation from the docs folder into html.
#set -x
init_vars () {
    BD_TOOLS="$PWD/tools/build-docs"
    PATCH_FILE="$BD_TOOLS/markdown-to-ascii.patch"
    REPO_FOLDER="$BD_TOOLS/org-asciidoc/"
    MD_REPO_FOLDER="$BD_TOOLS/markdown-to-asciidoc"
    DOCS_FOLDER="$PWD/docs"
    MD_CMD="$MD_REPO_FOLDER/build/scripts/markdown_to_asciidoc"
    DELETE_ADOC_AFTER=1
    REPO_BASE="$PWD"
}
init_vars
notef () {
    printf "$@" 1>&2
}
die () {
    notef "$@"
    exit 1
}

check_for_cmd () {
    if which "$1" > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}
find_gradle () {
    if check_for_cmd gradle; then
        printf "gradle"
    else
        IFS_SAVE="$IFS"
        IFS=":"
        THING=""
        for i in $PATH; do
            if [ -z "$THING" ] && [ -d "$i" ]; then
                THING=$(find "$i" -name 'gradle-*' | sort | head -n 1)
            fi
        done
        IFS="$IFS_SAVE"
        printf "%s" "$THING"
    fi
}
prepare_org_repo () {
    if ! check_for_cmd emacs; then
        die "Can't find emacs"
    fi
    if [ ! -d "$REPO_FOLDER" ]; then
        git clone https://github.com/yashi/org-asciidoc "$REPO_FOLDER" || exit "$?"
    fi
}
prepare_md_repo () {
    if [ ! -d "$MD_REPO_FOLDER" ]; then
        git clone https://github.com/bodiam/markdown-to-asciidoc "$MD_REPO_FOLDER" || exit "$?"
        cd -- "$MD_REPO_FOLDER"
        patch -p1 < "$PATCH_FILE"
    fi
    if [ ! -f "$MD_CMD" ]; then
        cd -- "$MD_REPO_FOLDER" || exit "$?"
        GRADLE=$(find_gradle)
        if [ "$GRADLE" ]; then
            notef "have gradle $GRADLE\n"
        else
            die "no have gradle\n"
        fi
        TERM=xterm "$GRADLE" build 1>&2 || exit "$?"
        mv build/libs build/lib || exit "$?"
    fi
}

get_highlight_option () {
    HIGHLIGHT_OPTION=source-highlighter=pygments
    if check_for_cmd gem; then
        if gem list | grep -F pygments.rb >/dev/null ; then
            notef "Have pygments.rb, continuing with highlighting\n"
        else
            notef "Don't have pygments.rb, trying to install\n"
            gem install pygments.rb
            if [ "$?" != 0 ]; then
                HIGHLIGHT_OPTION=source-highlighter!
                notef "Failed installing pygments.rb, disabling highlighting\n"
            fi
        fi
    else
        notef "Don't have command gem. Disabling highlighting\n"
    fi
    printf "%s" "$HIGHLIGHT_OPTION"
}
convert_org_to_asciidoc () {
    SOURCE_FILE="$1"
    OUTPUT_FILE="$2"
    LOAD_LIB_CMD="(add-to-list 'load-path \"$REPO_FOLDER\")
                  (require 'ox-asciidoc)"
    WRITE_CMD="(write-file \"$OUTPUT_FILE\")"
    TEMPFILE=$(tempfile)
    printf "%s" "$LOAD_LIB_CMD" > "$TEMPFILE"
    emacs -batch "$SOURCE_FILE" --load "$TEMPFILE" -f org-asciidoc-export-as-asciidoc --eval "$WRITE_CMD"
    #emacs -batch "$SOURCE_FILE" --eval "$LOAD_LIB_CMD (org-asciidoc-export-as-asciidoc) $WRITE_CMD" 1>&2 || exit "$?"
}
convert_md_to_asciidoc () {
    SOURCE_FILE="$1"
    OUTPUT_FILE="$2"
    #echo "$SOURCE_FILE"
    $MD_CMD "$SOURCE_FILE" > "$OUTPUT_FILE" || exit "$?"
}
if [ ! -d "$BD_TOOLS" ]; then
    cd ..
    init_vars
    if [ ! -d "$BD_TOOLS" ]; then
        cd ..
        init_vars
        if [ ! -d "$BD_TOOLS" ]; then
            die "You are in the wrong folder\n"
        fi
    fi
fi
prepare_md_repo
prepare_org_repo
if [ "$1" == org2adoc ]; then
    #shift
    set -x
    convert_org_to_asciidoc "$(readlink -f "$2")" "$(readlink -f "$3")"
    exit "$?"
fi
declare -a added_files
for i in $(find "$DOCS_FOLDER" -name '*.org'); do
    SOURCE_FILE="$i"
    OUTPUT_FILE="${SOURCE_FILE/.*/}.asciidoc"
    convert_org_to_asciidoc "$SOURCE_FILE" "$OUTPUT_FILE"
    added_files+=("$OUTPUT_FILE")
done
for i in $(find "$DOCS_FOLDER" -regex '.*\.md\|.*\.markdown'); do
    SOURCE_FILE="$i"
    OUTPUT_FILE="${SOURCE_FILE/.*/}.asciidoc"
    convert_md_to_asciidoc "$SOURCE_FILE" "$OUTPUT_FILE"
    added_files+=("$OUTPUT_FILE")
done
if [ ! -f "$DOCS_FOLDER/README.asciidoc" ]; then
    die "no file"
fi
fix_readme_links () {
    NODOT="[^[]"
    NO="[^[]"
    sed -Ei "s/link:($NO+)\.(asciidoc|adoc|md|markdown|org)\[($NO+)\]/<<\1.asciidoc#,\3>>/g" "$DOCS_FOLDER/README.asciidoc" || exit "$?"
    # Maybe uncomment later if we get the ChangeLog converting to asciidoc
    #sed -Ei "s/link:(ChangeLog)\[($NO+)\]/<<\1.asciidoc#,\2>>/g" "$DOCS_FOLDER/README.asciidoc" || exit "$?"
}
fix_readme_links
asciidoctor \
    -a relative-ext=.html \
    -a "$(get_highlight_option)" \
    -a tip-caption=💡 \
    -a note-caption=🛈 \
    -a important-caption=❗ \
    -a caution-caption=🔥 \
    -a warning-caption=⚠️ \
    -a author! \
    "$DOCS_FOLDER"/*.asciidoc "$DOCS_FOLDER/jit"/*.asciidoc || die "$?"
mv -- "$DOCS_FOLDER/README.html" "$DOCS_FOLDER/index.html"
if [ "$DELETE_ADOC_AFTER" == 1 ]; then
    for i in ${added_files[@]}; do
        rm -- "$i"
    done
fi
