<?php
declare(strict_types=1);

$repoDir = __DIR__ . '/packages';

function pkg_valid_name(string $name): bool {
    return preg_match('/^[A-Za-z0-9_.-]{1,63}$/', $name) === 1;
}

function pkg_base_url(): string {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off');
    $scheme = $https ? 'https' : 'http';
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';
    $script = $_SERVER['SCRIPT_NAME'] ?? '/index.php';
    return $scheme . '://' . $host . $script;
}

function pkg_header_text(): void {
    header('Content-Type: text/plain; charset=utf-8');
}

function pkg_not_found(string $message): never {
    http_response_code(404);
    pkg_header_text();
    echo $message . "\n";
    exit;
}

function pkg_package_dir(string $repoDir, string $name): string {
    return $repoDir . '/' . $name;
}

function pkg_read_meta(string $dir): array {
    $meta = [
        'version' => '1.0.0',
        'target' => '',
        'description' => '',
    ];
    $path = $dir . '/package.ini';
    if (is_file($path)) {
        $ini = parse_ini_file($path, false, INI_SCANNER_RAW);
        if (is_array($ini)) {
            foreach (['version', 'target', 'description'] as $key) {
                if (isset($ini[$key]) && is_string($ini[$key])) {
                    $meta[$key] = trim($ini[$key]);
                }
            }
        }
    }
    return $meta;
}

function pkg_manifest(string $repoDir, string $name): never {
    if (!pkg_valid_name($name)) {
        pkg_not_found('invalid package name');
    }
    $dir = pkg_package_dir($repoDir, $name);
    $elf = $dir . '/' . $name . '.elf';
    if (!is_file($elf)) {
        pkg_not_found('package not found');
    }

    $meta = pkg_read_meta($dir);
    $target = $meta['target'] !== '' ? $meta['target'] : '/shell/' . $name . '.elf';
    $download = pkg_base_url() . '?download=' . rawurlencode($name);

    pkg_header_text();
    echo "format=cleonos-pkg-v1\n";
    echo "name=" . $name . "\n";
    echo "version=" . $meta['version'] . "\n";
    echo "target=" . $target . "\n";
    echo "url=" . $download . "\n";
    if ($meta['description'] !== '') {
        echo "description=" . str_replace(["\r", "\n"], ' ', $meta['description']) . "\n";
    }
    exit;
}

function pkg_download(string $repoDir, string $name): never {
    if (!pkg_valid_name($name)) {
        pkg_not_found('invalid package name');
    }
    $elf = pkg_package_dir($repoDir, $name) . '/' . $name . '.elf';
    if (!is_file($elf)) {
        pkg_not_found('package not found');
    }

    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . (string)filesize($elf));
    header('Content-Disposition: attachment; filename="' . $name . '.elf"');
    readfile($elf);
    exit;
}

if (isset($_GET['manifest'])) {
    pkg_manifest($repoDir, (string)$_GET['manifest']);
}

if (isset($_GET['download'])) {
    pkg_download($repoDir, (string)$_GET['download']);
}

$packages = [];
if (is_dir($repoDir)) {
    foreach (scandir($repoDir) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..' || !pkg_valid_name($entry)) {
            continue;
        }
        if (is_file(pkg_package_dir($repoDir, $entry) . '/' . $entry . '.elf')) {
            $packages[] = $entry;
        }
    }
}
sort($packages);
?><!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>CLeonOS package repository</title>
</head>
<body>
<h1>CLeonOS package repository</h1>
<p>Client usage: pkg repo <?php echo htmlspecialchars(pkg_base_url(), ENT_QUOTES, 'UTF-8'); ?></p>
<p>Install example: pkg install hello</p>
<h2>Packages</h2>
<?php if (count($packages) === 0): ?>
<p>No packages.</p>
<?php else: ?>
<ul>
<?php foreach ($packages as $package): ?>
<li>
<?php echo htmlspecialchars($package, ENT_QUOTES, 'UTF-8'); ?>
-
<a href="?manifest=<?php echo rawurlencode($package); ?>">manifest</a>
-
<a href="?download=<?php echo rawurlencode($package); ?>">download</a>
</li>
<?php endforeach; ?>
</ul>
<?php endif; ?>
</body>
</html>
