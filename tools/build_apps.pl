use strict;
use Cwd qw(cwd);
use Term::ANSIColor;

my %target = (
    "peer_demo"         => [qw/esp32 esp32s3 esp32s2/],
    "doorbell_demo"     => [qw/esp32p4 esp32s3/],
    "openai_demo"       => [qw/esp32s3/],
    "videocall_demo"    => [qw/esp32p4/],
    "whip_demo"         => [qw/esp32p4/],
    "doorbell_local"    => [qw/esp32p4/],
    "janus_demo"        => [qw/esp32p4/],
    "kms_demo"          => [qw/esp32p4 esp32s3/],
    "kvs_master"        => [qw/esp32p4 esp32s3/],
    "rtsp_demo"         => [qw/esp32p4/],
    "rtmp_demo"         => [qw/esp32p4/],
    "local_jpeg_stream" => [qw/esp32p4/],
);

my %prefer_board = (
    "esp32p4" => 'esp32_p4_function_ev_board',
    "esp32s3" => 'esp32_s3_korvo_2_3',
);

# Apps that do not use esp_board_manager (keep set-target path)
my %no_board_manager = map { $_ => 1 } qw/peer_demo/;

my $cur = cwd();
my ($app_folder, $target);
my $with_board_manager = 1;  # default on for migrated demos
my $build_all = 0;
if (@ARGV >= 2) {
    $app_folder = $ARGV[0];
    $target = $ARGV[1];
    if (@ARGV >= 3) {
        $with_board_manager = $ARGV[2];
    }
    # Disable board manager for apps that never use it
    my $app_name = $app_folder;
    $app_name =~ s/.*\///;
    if ($no_board_manager{$app_name}) {
        $with_board_manager = 0;
    }
    build_target($app_folder, $target);
} else {
    $build_all = 1;
    for my $t (keys %target) {
        my @board = @{$target{$t}};
        print "Start build for $t @board\n";
        for (@board) {
            $with_board_manager = $no_board_manager{$t} ? 0 : 1;
            build_target("$cur/$t", $_);
        }
    }
}

sub filter_build {
    my $target = shift;
    my $build_cmd;
    if ($with_board_manager) {
        my $board = $prefer_board{$target};
        die "No preferred board for target $target\n" unless $board;
        # Ensure idf.py board-manager extension is available before gen-bmgr-config
        $build_cmd = "pip install --upgrade esp-bmgr-assist 2>&1; "
                   . "idf.py gen-bmgr-config -b $board 2>&1; "
                   . "idf.py build 2>&1";
    } else {
        $build_cmd = "idf.py set-target $target 2>&1; idf.py build 2>&1";
    }
    open(my $fh, '-|', $build_cmd) or die "Failed to run build command: $!";
    my @error_patterns = (
        qr/\b(error|warning)\b/i,
        qr/failed to build/i,
        qr/link error/i,
        qr/undefined reference/i,
    );
    while (my $line = <$fh>) {
        chomp $line;
        if (grep { $line =~ $_ } @error_patterns) {
            print colored($line, 'red'), "\n";
        }
    }
    close($fh);
}

sub build_target {
    my ($app_dir, $target) = @_;
    chdir($app_dir);
    my $app = $app_dir;
    print "Build for $app_dir $target (board_manager=$with_board_manager)\n";
    $app =~ s/.*\///;
    `rm -rf $_` for (qw/dependencies.lock build managed_components sdkconfig/);
    filter_build($target);
    my $f = <build/*.elf>;
    if ($f) {
        print colored("Build for $app target $target success\n", 'green');
    } else {
        print colored("Fail to build for $app target $target\n", 'red');
        exit(-1) if ($build_all == 0);
    }
}
