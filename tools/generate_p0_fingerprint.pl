#!/usr/bin/env perl
use strict;
use warnings;

@ARGV == 3 or die
    "usage: $0 RAW_IMAGE PROBE_OFFSET OUTPUT_HEADER\n";
my ($image_path, $probe_text, $output_path) = @ARGV;

$probe_text =~ /\A(?:0x)?[0-9a-fA-F]+\z/
    or die "invalid probe offset: $probe_text\n";
my $probe_offset = hex($probe_text);

open my $image_fh, '<:raw', $image_path
    or die "open $image_path: $!\n";
local $/;
my $image = <$image_fh>;
close $image_fh or die "close $image_path: $!\n";

my @page_offsets = (0x000, 0x200, 0x400, 0x600,
                    0x800, 0xa00, 0xc00, 0xe00);
my @rows;
for my $slide (map { $_ * 0x10000 } 0 .. 31) {
    my $page_source = $probe_offset - $slide;
    $page_source >= 0
        or die sprintf("slide 0x%x exceeds probe offset 0x%x\n",
                       $slide, $probe_offset);

    my @words;
    for my $page_offset (@page_offsets) {
        my $source_offset = $page_source + $page_offset;
        $source_offset + 8 <= length($image)
            or die sprintf("source offset 0x%x exceeds image\n",
                           $source_offset);
        push @words, unpack('Q<', substr($image, $source_offset, 8));
    }
    push @rows, [$slide, $page_source, \@words];
}

open my $out, '>', $output_path
    or die "open $output_path: $!\n";
print {$out} <<"HEADER";
// Generated from the exact raw Image.
// Each row maps actual slide to Image[0x@{[sprintf '%x', $probe_offset]} - slide].
#ifndef P0_FINGERPRINT_H
#define P0_FINGERPRINT_H

#define P0_FINGERPRINT_WORDS 8

static const uint16_t p0_fingerprint_offsets[P0_FINGERPRINT_WORDS] = {
  0x000, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,
};

struct p0_fingerprint {
  uintptr_t slide;
  uint64_t words[P0_FINGERPRINT_WORDS];
};

static const struct p0_fingerprint p0_fingerprints[] = {
HEADER
for my $row (@rows) {
    my ($slide, undef, $words) = @$row;
    printf {$out} "  { 0x%06xULL, { ", $slide;
    for my $index (0 .. $#$words) {
        printf {$out} "0x%016xULL", $words->[$index];
        print {$out} $index == $#$words ? " } },\n" :
                     $index % 2 == 1 ? ",\n    " : ", ";
    }
}
print {$out} <<'FOOTER';
};

#endif
FOOTER
close $out or die "close $output_path: $!\n";

# Independent readback from a newly opened file handle.
open my $verify_fh, '<:raw', $image_path
    or die "reopen $image_path: $!\n";
for my $row (@rows) {
    my ($slide, $page_source, $words) = @$row;
    for my $index (0 .. $#page_offsets) {
        my $source_offset = $page_source + $page_offsets[$index];
        seek($verify_fh, $source_offset, 0)
            or die sprintf("seek 0x%x: %s\n", $source_offset, $!);
        read($verify_fh, my $bytes, 8) == 8
            or die sprintf("short read at 0x%x\n", $source_offset);
        my $actual = unpack('Q<', $bytes);
        $actual == $words->[$index]
            or die sprintf("mismatch for slide 0x%x at source 0x%x\n",
                           $slide, $source_offset);
    }
}
close $verify_fh or die "close verification input: $!\n";
printf "verified 32 rows and 256 source qwords at probe 0x%x\n",
       $probe_offset;
