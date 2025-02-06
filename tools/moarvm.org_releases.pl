#!/usr/bin/env perl
use strict;
use warnings;
use utf8;
use feature qw(unicode_strings say);
use English '-no_match_vars';
binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
use Text::Markdown 'markdown';

sub slurp {
  my ($filename) = @_;
  local $INPUT_RECORD_SEPARATOR = undef;
  open my $fh, '<', $filename;
  binmode $fh, ':encoding(UTF-8)';
  return <$fh>;
}
sub main {
    my $ChangeLog = slurp 'docs/ChangeLog';
    $ChangeLog =~ s/(^\s*[^+].*?:\n[ ]*)([+])/$1\n$2/xmsg;
    say beginning_text() . process_changelog(html_escape($ChangeLog)) . end_text();
}
main();
sub process_changelog {
    my ($in) = @_;
    my @release_html;
    # Extracts the text from the 'New in XXXX.XX' to the next 'New in XXXX.XX'
    # if there are no 'New in' left it extracts to the end of the document.
    while ($in =~ s/(New in ([\d.]+)(.*?))(New in [\d.]+|$)/$4/s) {
        my $sec = $3;
        my $release = $2;
        print STDERR "$release\n";
        my $md_obj    = Text::Markdown->new;
        my $mid = $md_obj->markdown($sec);
my $start = <<"END";
<div class="panel panel-default">
        <div class="panel-heading">
          <h3 class="panel-title">$release</h3>
        </div>
        <div class="panel-body">
END
        my $end = <<"END";
<p><a href="releases/MoarVM-{$release}.tar.gz" class="btn btn-primary" role="button">Download</a></p>
</div>
</div>
END
        my $html = start_enclosing_text($release) . $mid . end_enclosing_text($release);
        push @release_html, $html;
    }
    return join '', @release_html;
}
# Escapes &, < and > for html
sub html_escape {
  my ($text) = @_;
  $text =~ s/[&]/&amp;/g;
  $text =~ s/[<]/&lt;/g;
  $text =~ s/[>]/&gt;/g;
  return $text;
}
# The text comes before every version
sub start_enclosing_text {
  my ($release) = @_;
  my $start = <<"END";
<div class="panel panel-default">
        <div class="panel-heading">
          <h3 class="panel-title">$release</h3>
        </div>
        <div class="panel-body">
END
  return $start;
}
# This text comes after every version
sub end_enclosing_text {
  my ($release) = @_;
  my $end = <<"END";
<p><a href="releases/MoarVM-$release.tar.gz" class="btn btn-primary" role="button">Download</a></p>
</div>
</div>
END
  return $end;
}
# This text begins the html document (only used once).
sub beginning_text {
  <<'END';
<!DOCTYPE html>
<html lang="en-US">
  <head>
    <title>MoarVM - A VM for Rakudo and NQP</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link href="css/bootstrap.min.css" rel="stylesheet">
    <link href="css/local.css" rel="stylesheet">

    <!-- HTML5 Shim and Respond.js IE8 support of HTML5 elements and media queries -->
    <!-- WARNING: Respond.js doesn't work if you view the page via file:// -->
    <!--[if lt IE 9]>
      <script src="https://oss.maxcdn.com/libs/html5shiv/3.7.0/html5shiv.js"></script>
      <script src="https://oss.maxcdn.com/libs/respond.js/1.3.0/respond.min.js"></script>
    <![endif]-->
  </head>
  <body>
    <div class="container">

      <a href="https://github.com/MoarVM/MoarVM"><img style="position: absolute; top: 0; right: 0; border: 0;" src="https://s3.amazonaws.com/github/ribbons/forkme_right_gray_6d6d6d.png" alt="Fork me on GitHub"></a>

      <div class="page-header" id="banner">
        <h1>MoarVM</h1>
        <p class="lead">A VM for Rakudo and NQP</p>
      </div>
      <nav class="navbar navbar-default" role="navigation">
        <ul class="nav navbar-nav">
          <li><a href="index.html">Home</a></li>
          <li><a href="features.html">Features</a></li>
          <li><a href="roadmap.html">Roadmap</a></li>
          <li class="active"><a href="releases.html">Releases</a></li>
          <li><a href="contributing.html">Contributing</a></li>
        </ul>
      </nav>
END
}
# This text ends the html document (only used once).
sub end_text {
<<'END';

      <footer>
        <div class="row">
          <div class="col-lg-12">
              <p>Site maintained by the MoarVM team.</p>
              <p>Based on <a href="http://getbootstrap.com" rel="nofollow">Bootstrap</a>,
              with theme from <a href="http://bootswatch.com/">Bootswatch</a>. Icons from
              <a href="http://fortawesome.github.io/Font-Awesome/" rel="nofollow">Font Awesome</a>.
              Web fonts from <a href="http://www.google.com/webfonts" rel="nofollow">Google</a>.
          </div>
        </div>
      </footer>
    </div>

    <!-- jQuery (necessary for Bootstrap's JavaScript plugins) -->
    <script src="https://code.jquery.com/jquery.js"></script>
    <!-- Include all compiled plugins (below), or include individual files as needed -->
    <script src="js/bootstrap.min.js"></script>
  </body>
</html>
END
}
