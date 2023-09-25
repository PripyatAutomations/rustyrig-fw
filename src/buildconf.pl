#!/usr/bin/perl
#
# Convert $profile.json to an eeprom.bin and appropriate headers needed to build
# the radio software, either natively or cross compiled.
# $profile defaults to 'radio' but can be provided as the first (and only) argument
###############################################################################
use strict;
use warnings;
use Config;
use Getopt::Std;
use IO::Handle;
use JSON;
use Data::Dumper;
use MIME::Base64;
use Hash::Merge qw( merge );
use Mojo::JSON::Pointer;
use String::CRC32;
use File::Path qw(make_path remove_tree);

Hash::Merge::set_behavior('RIGHT_PRECEDENT');
$Data::Dumper::Terse = 1;
$Data::Dumper::Indent = 1;       # mild pretty print
$Data::Dumper::Useqq = 1;        # print strings in double quotes
STDOUT->autoflush(1);

#############################
# Determine Project Version #
#############################
open(my $verfh, "<", ".version") or die("Missing .version!\n");
my $verdata = '';
while (<$verfh>) {
   $verdata = $verdata . $_;
}
close($verfh);

# remove spaces and newlines
$verdata =~ s/^\s+|\s(?=\s)|\s+$//g;
my @ver = split(/\./, $verdata);
my $version = {
   "firmware" => {
      "major" => $ver[0],
      "minor" => $ver[1]
   }
};

############
# Defaults #
############
my $default_cfg = {
   "eeprom" => {
      "size" => 16384,
      "type" => "mmap",
      "addr" => 0x0000
   }
};
# Merge the version information into the default configuration
my $base_cfg = merge($default_cfg, $version);

###########
# Globals #
###########
my $profile = 'radio';

# use profile name from command line, if given
if ($#ARGV == 0) {
   $profile = $ARGV[0];
}

my $build_dir = "build/$profile";
my $config = { };
my $config_file = "$profile.json";
my $cptr;
my $eeprom_file = "$build_dir/eeprom.bin";
my $eeprom_data = '';
my @eeprom_layout;
my @eeprom_types;

# Make the build directory tree, if not present
make_path("$build_dir/obj");

# Load configuration from $config_file
sub config_load {
   print "* Load config from $_[0]\n";
   open(my $fh, '<', $_[0]) or die("  * Couldn't open configuration file $_[0], please make sure it exists!\n");
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($fh);

   $nbytes = length($tmp);
   print "  * Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   my $loaded_cfg = $json->decode($tmp) or die("ERROR: Can't parse configuration $_[0]!\n");

   # Merge defaults with loaded configuration
   $config = merge($base_cfg, $loaded_cfg);

   # apply mojo json pointer, so we can reference by path
   $cptr = Mojo::JSON::Pointer->new($config);
}

sub eeprom_types_load {
   print "* Load EEPROM data types from $_[0]\n";
   open(my $fh, '<', $_[0]) or warn("  * Couldn't open configuration file $_[0], using defaults!\n");
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($fh);

   $nbytes = length($tmp);
   print "  * Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   @eeprom_types = $json->decode($tmp) or warn("ERROR: Can't parse EEPROM types file $_[0]!\n") and return;

   # Iterate over the types and print them...
   for my $item (@eeprom_types) {
      for my $key (sort keys(%$item)) {
          my $ee_size = $item->{$key}{size};
          if ($ee_size == -1) {
             $ee_size = 'variable';
          }
          print "  * Registering EEPROM data type: '$key' with size $ee_size\n";
      }
   }
}

# Load eeprom_layout from eeprom_layout.json
sub eeprom_layout_load {
   print "* Load EEPROM layout definition from $_[0]\n";
   open(my $fh, '<', $_[0]) or die("ERROR: Couldn't open $_[0]!\n");
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($fh);

   $nbytes = length($tmp);
   print "  * Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   @eeprom_layout = $json->decode($tmp) or warn("ERROR: Can't parse $_[0]!\n") and return;
}

sub eeprom_load {
   my $eeprom_size = $cptr->get("/eeprom/size");
   if (! -e $_[0]) {
      print "* EEPROM image $_[0] doesn't exist, using empty $eeprom_size byte image!\n";
      return;
   }

   print "* Load EEPROM data from $_[0]\n";
   open(my $EEPROM, '<:raw', $_[0]) or print "  * Not found, skipping\n" and return;
   my $nbytes = 0;

   while (1) {
      my $res = read $EEPROM, $eeprom_data, 100, length($eeprom_data);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($EEPROM);

   $nbytes = length($eeprom_data);

   if ($nbytes == $eeprom_size) {
      print "  * Read $nbytes bytes from $_[0]\n";
   } elsif ($nbytes == 0) {
      print "  * Read empty EEPROM from $_[0]... Fixing!\n";
      return;
   } else {
      die("ERROR: Expected $eeprom_size bytes but read $nbytes from $_[0], halting to avoid damage...\n");
   }

   # Verify the checksum of the EEPROM, before allowing it to be used as source...
   eeprom_verify_checksum();
}

sub eeprom_patch {
   my $changes = 0;
   my $errors = 0;
   my $warnings = 0;
   my $eeprom_size = $cptr->get("/eeprom/size");
   my $curr_offset = 0;

   print "* Patch in-memory image\n";

   if ($eeprom_data eq "") {
      $eeprom_data = "\x00" x $eeprom_size;
   }

   # Walk over the layout structure and see if we have a value set in the config...
   for my $item (@eeprom_layout) {
       for my $key (sort { $item->{$a}{offset} <=> $item->{$b}{offset} } keys %$item) {
          my $ee_key = $item->{$key}{key};
          my $ee_default = $item->{$key}{default};
          my $ee_off_raw = $item->{$key}{offset};
          my $ee_offset = $ee_off_raw;
          my $ee_offset_relative = 0;
          my $ee_size = $item->{$key}{size};
          my $ee_type = $item->{$key}{type};
          my $eeprom_size = $cptr->get("/eeprom/size");
          my $cval;
          my $defval = 0;

          if (defined($ee_off_raw)) {
             if ($ee_off_raw =~ m/^\+/) {
                # Is this a relative offset?
                $ee_offset_relative = 1;
                $ee_offset =~ s/\+//;
                $curr_offset += $ee_offset;
   #             print "* RELative OFFset: $curr_offset\n";
             } else {
                # Absolute offset
                $ee_offset_relative = 0;
                $curr_offset = $ee_offset;
   #             print "* ABSolute OFFset: $curr_offset\n";
             }
          }

          if (!defined($ee_key)) {
#             printf "*skip* no key\n";
             next;
          }

          $changes++;

          # skip unsaved keys (checksum mostly)
          my $ee_protect = $item->{$key}{protect};
          if (defined($ee_protect)) {
             print "  => Skipped key $ee_key, it is protected.\n";
             next;
          }

          $cval = $cptr->get("/$ee_key");
          my $ckey = $ee_key;
          $ckey =~ s/\//_/;

          if (!defined($cval)) {
             $warnings++;
             if (defined($ee_default)) {
                $defval = 1;
                $cval = $ee_default;
             } else {
                printf "  => Not patching %d byte%s @ <%04d>: key $ee_key: <Warning - no value set!>\n", $ee_size, ($ee_size == 1 ? " " : "s"), $curr_offset;
                next;
             }
          }


          # Validate the offset and size will fit within the rom
          if (($ee_offset >= 0 && $ee_offset < $eeprom_size) &&
              ($curr_offset + $ee_size < $eeprom_size) &&
              ($ee_offset + $ee_size <= $eeprom_size)) {

             printf "  => Patching %d byte%s @ <%04d>: [%s] %s = %s%s\n", $ee_size, ($ee_size == 1 ? " " : "s"), $curr_offset, $ee_type, $ee_key, $cval, ($defval == 0 ? "" : " <Warning - default value!>");

             # Pad out the memory location with NULLs to get rid of old contents...
             substr($eeprom_data, $curr_offset, $ee_size, "\x00" x $ee_size);

             # Here we chose which type
             # XXX: Need to use eeprom_types sizes, etc here - overriding as needed...
             if ($ee_type eq 'call') {
               # callsign (8 bytes)
               my $packedcall = pack("Z8", $cval);
               substr($eeprom_data, $curr_offset, 8, $packedcall);
             } elsif ($ee_type eq 'class') {
                # license privileges (1 byte enum)
                if ($cval =~ "US/Technician") {
                   $cval = 'T';
                } elsif ($cval =~ "US/General") {
                   $cval = 'G';
                } elsif ($cval =~ "US/Advanced") {
                   $cval = 'A';
                } elsif ($cval =~ "US/Extra") {
                   $cval = 'E';
                }
               substr($eeprom_data, $curr_offset, 1, pack("a1", $cval));
             } elsif ($ee_type eq 'int') {
                # integer (4 bytes)
                if ($cval =~ m/^0x/) {
                   # Convert hex string cval to int cval for embedding...
                   $cval = hex($cval);
                }
               substr($eeprom_data, $curr_offset, 4, pack("I", $cval));
             } elsif ($ee_type eq 'ip4') {
                # ipv4 address (4 bytes)
                my @tmpip = split(/\./, $cval);
                my $packedip = pack("CCCC", $tmpip[0], $tmpip[1], $tmpip[2], $tmpip[3]);
               substr($eeprom_data, $curr_offset, 4, $packedip);
             } elsif ($ee_type eq 'str') {
                # string (variable length)
                if ($ee_size == 0) {
                   print "   * Skipping 0 length key\n";
                   next;
                }
                substr($eeprom_data, $curr_offset, $ee_size, $cval);
             }

             # Try to make sure the offsets are lining up....
             if ($curr_offset != $ee_offset) {
                printf "Calculated offset (%04d) and layout offset (%04d) mismatch, halting to avoid damage!\n", $curr_offset, $ee_offset;
                die();
             }
             $curr_offset += $ee_size;
          } else {
             print "Invalid offset/size ($ee_offset, $ee_size) - our EEPROM is $eeprom_size\n";
          }
       }
   }

   if ($errors > 0) {
      print "*** There were $errors errors while processing $changes patches, aborting without change!\n";
      die();
   }

   print "* Finished patching, there were $warnings warnings from $changes patches\n";
}

# Confirm the checksum is correct
sub eeprom_verify_checksum {
   print "* Verifying image checksum...\n";
   my $eeprom_size = $cptr->get("/eeprom/size");

   # XXX: This should get the offset from @eeprom_layout!
   my $ed_offset = ($eeprom_size - 4);
   my $chksum = crc32(substr($eeprom_data, 0, -4));
   my $saved_chksum = unpack("L", substr($eeprom_data, $ed_offset, 4));

   if ($saved_chksum eq "\0\0\0\0") {
      printf "  * Ignoring empty EEPROM checksum\n";
   } elsif ($chksum eq $saved_chksum) {
      printf "  * Valid checksum (CRC32) <%x>, continuing!\n", $chksum;
   } else {
      printf "  * Stored checksum (CRC32) <%x> doesn't match calculated <%x>, bailing to prevent damage!\n", $saved_chksum, $chksum;
      return 1;
   }
}

# Update the in-memory image's checksum before saving
sub eeprom_update_checksum {
   my $chksum = crc32(substr($eeprom_data, 0, -4));
   my $eeprom_size = $cptr->get("/eeprom/size");
   printf "* Updating in-memory EEPROM image checksum to <%x>\n", $chksum;

   # XXX: This should get the offset from @eeprom_layout!
   my $ed_offset = ($eeprom_size - 4);

   # patch the checksum
   substr($eeprom_data, $ed_offset, 4, pack("l", $chksum));
}

# Write out our EEPROM, if it looks valid
sub eeprom_save {
   my $nbytes = length($eeprom_data);
   my $eeprom_size = $cptr->get("/eeprom/size");

   # Do some sanity checks on the EEPROM...
   if ($nbytes == 0) {
      die("ERROR: Refusing to write empty (0 byte) EEPROM to $_[0]\n");
   }

   if ($nbytes != $eeprom_size) {
      die("ERROR: Our EEPROM ($_[0]) is $eeprom_size bytes, but we ended up with $nbytes bytes of data. Halting to avoid damage!\n");
   }

   # If we made it here, update the eeprom checksum...
   eeprom_update_checksum();

   # Write out the eeprom image
   print "* Saving EEPROM to $_[0]\n";
   open(my $EEPROM, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");
   print $EEPROM $eeprom_data or die("write error: $_[0]: $!\n");
   close $EEPROM;

   print "  * Wrote $nbytes bytes to $_[0]\n";
}

# Save the key to offset/size/type mappings
sub generate_eeprom_layout_h {
   my $file = $_[0];
   my $nbytes = 0;

   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }

   print "  * Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $fh "// This file is autogenerated by buildconf, please do not edit it directly.\n";
   print $fh "// Please edit $config_file then re-run './buildconf.pl $config_file' to make changes.\n";
   print $fh "#if     !defined(_eeprom_layout_h)\n#define _eeprom_layout_h\n";

   my $eeprom_size = $cptr->get("/eeprom/size");
   print $fh "struct eeprom_layout eeprom_layout[] = {\n";


   # this needs reworked so that we get the enum types out of eeprom_types
   # XXX: We also should generate the eeprom_layout.type enum here, so we include all types...
   for my $item (@eeprom_layout) {
      for my $key (sort keys(%$item)) {
          my $ee_key = $item->{$key}{key};
          my $ee_offset = $item->{$key}{offset};
          my $ee_size = $item->{$key}{size};
          my $ee_type = $item->{$key}{type};
          my $ee_type_enum = "EE_NONE";

          if ($ee_type eq "byte") {
             $ee_type_enum = "EE_BYTES";
          } elsif ($ee_type eq "float") {
             $ee_type_enum = "EE_FLOAT";
          } elsif ($ee_type eq "int") {
             $ee_type_enum = "EE_INTEGER";
          } elsif ($ee_type eq "ip4") {
             $ee_type_enum = "EE_IP4";
          } elsif ($ee_type eq "grid") {
             $ee_type_enum = "EE_GRID";
          } elsif ($ee_type eq "class") {
             $ee_type_enum = "EE_LCLASS";
          } elsif ($ee_type eq "str") {
             $ee_type_enum = "EE_STRING";
          }

          if (defined($ee_key)) {
             my $cval = $cptr->get("/$ee_key");
             if (defined($cval)) {
                print $fh "   { .key = \"$ee_key\", .type = $ee_type_enum, .offset = $ee_offset, .size = $ee_size },\n"
             }
          }
       }
   }
   print $fh "};\n";
   print $fh "#endif\n";
   close $fh;
}

# Export a build configuration, to include the needed parts to support our configuration....
sub generate_config_h {
   my $file = $_[0];
   my $nbytes = 0;

   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }

   print "  * Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $fh "// Auto-generated file. Please do not edit. Run buildconf.pl instead\n\n";
   print $fh "#if !defined(_config_h)\n#define _config_h\n";
   printf $fh "#define VERSION \"%02d.%02d\"\n", $version->{firmware}{major}, $version->{firmware}{minor};
   printf $fh "#define VERSION_MAJOR 0x%x\n", $version->{firmware}{major};
   printf $fh "#define VERSION_MINOR 0x%x\n", $version->{firmware}{minor};

   if (defined($config->{i2c})) {
      if (defined($config->{i2c}{myaddr})) {
         printf $fh "#define MY_I2C_ADDR %s\n", $config->{i2c}{myaddr};
      }
   }

   if (!defined($config->{eeprom}{type}) || !defined($config->{eeprom}{addr})) {
      die("ERROR: Invalid configuration, edit $file and try again!\n");
   }

   if ($config->{eeprom}{type} eq "mmap") {
      print $fh "#define EEPROM_TYPE_MMAP true\n";
      print $fh "#undef EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_MMAP_ADDR %s\n", $config->{eeprom}{addr};
   } elsif ($config->{eeprom}{type} eq "i2c") {
      print $fh "#undef EEPROM_TYPE_MMAP\n";
      print $fh "#define EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_I2C_ADDR %s\n", $config->{eeprom}{addr};
   }

   if (defined($config->{features})) {
      if (defined($config->{features}{'cat-kpa500'}) && $config->{features}{'cat-kpa500'} eq 1) {
         printf $fh "#define CAT_KPA500 true\n";
      }
      if (defined($config->{features}{'cat-yaesu'}) && $config->{features}{'cat-yaesu'} eq 1) {
         printf $fh "#define CAT_YAESU true\n";
      }
   }

   if (!defined($config->{eeprom}{size})) {
      die("ERROR: eeprom.size not set!\n");
   }

   printf $fh "#define EEPROM_SIZE %d\n", $config->{eeprom}{size};

   my $max_bands = '';
   if (defined($config->{features}{'max-bands'})) {
      $max_bands = $config->{features}{'max-bands'};
   } else {
      $max_bands = 10;
   }
   printf $fh "#define MAX_BANDS %d\n", $max_bands;

   # Platform specific selections:
   my $platform = $cptr->get("/build/platform");
   if ($platform eq "posix") {
      print $fh "#define HOST_EEPROM_FILE \"$eeprom_file\"\n";
      print $fh "#define HOST_LOG_FILE \"firmware.log\"\n";
      print $fh "#define HOST_CAT_PIPE \"cat.fifo\"\n";
      print $fh "#define HOST_POSIX\n";
   }

   # footer
   print $fh "#endif\n";
   close $fh;
}

# Save all headers
sub generate_headers {
   print "* Generating headers\n";
   generate_config_h($build_dir . "/build_config.h");
   generate_eeprom_layout_h($build_dir . "/eeprom_layout.h");
}

#############################################################
print "* Host byte Order: ", $Config{byteorder}, "\n";

# Load the configuration
config_load($config_file);

# Load the EEPROM types definitions
eeprom_types_load("res/eeprom_types.json");

# Load the EEPROM layout definitions
eeprom_layout_load("res/eeprom_layout.json");

# Try loading the eeprom into memory
eeprom_load($eeprom_file);

# Apply loaded configuration to in-memory EEPROM image
eeprom_patch($config);

# Disable interrupt while saving
$SIG{INT} = 'IGNORE';

# Save the patched eeprom.bin
eeprom_save($eeprom_file);

# And generate headers for the C bits...
generate_headers();
