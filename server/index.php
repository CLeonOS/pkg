<?php
declare(strict_types=1);

session_start();

$repoDir = __DIR__ . '/packages';

const PKG_MAX_UPLOAD_BYTES = 33554432;

function pkg_valid_name(string $name): bool {
    return preg_match('/^[A-Za-z0-9_.-]{1,63}$/', $name) === 1;
}

function pkg_valid_username(string $name): bool {
    return preg_match('/^[A-Za-z0-9_.-]{3,32}$/', $name) === 1;
}

function pkg_valid_version(string $version): bool {
    return preg_match('/^[A-Za-z0-9_.:+~-]{1,64}$/', $version) === 1;
}

function pkg_clean_line(string $value, int $maxLen): string {
    $value = trim(str_replace(["\r", "\n"], ' ', $value));
    $value = preg_replace('/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/', '', $value) ?? '';
    if (strlen($value) > $maxLen) {
        $value = substr($value, 0, $maxLen);
    }
    return $value;
}

function pkg_has_prefix(string $text, string $prefix): bool {
    return substr($text, 0, strlen($prefix)) === $prefix;
}

function pkg_has_suffix(string $text, string $suffix): bool {
    if (strlen($suffix) > strlen($text)) {
        return false;
    }
    return substr($text, -strlen($suffix)) === $suffix;
}

function pkg_contains(string $text, string $needle): bool {
    return $needle === '' || strpos($text, $needle) !== false;
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

function pkg_header_json(): void {
    header('Content-Type: application/json; charset=utf-8');
}

function pkg_json(array $data, int $status = 200): never {
    http_response_code($status);
    pkg_header_json();
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT) . "\n";
    exit;
}

function pkg_error_json(string $message, int $status = 400): never {
    pkg_json([
        'ok' => false,
        'error' => $message,
    ], $status);
}

function pkg_not_found(string $message): never {
    http_response_code(404);
    pkg_header_text();
    echo $message . "\n";
    exit;
}

function pkg_html(string $value): string {
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}

function pkg_package_dir(string $repoDir, string $name): string {
    return $repoDir . '/' . $name;
}

function pkg_data_dir(): string {
    return dirname(__DIR__) . '/data';
}

function pkg_users_path(): string {
    return pkg_data_dir() . '/users.json';
}

function pkg_ensure_data_dir(): bool {
    $dir = pkg_data_dir();
    if (is_dir($dir)) {
        return true;
    }
    return mkdir($dir, 0775, true);
}

function pkg_load_users(): array {
    $path = pkg_users_path();
    if (!is_file($path)) {
        return ['users' => []];
    }

    $raw = file_get_contents($path);
    if ($raw === false || $raw === '') {
        return ['users' => []];
    }

    $data = json_decode($raw, true);
    if (!is_array($data) || !isset($data['users']) || !is_array($data['users'])) {
        return ['users' => []];
    }

    return $data;
}

function pkg_save_users(array $users): bool {
    if (!pkg_ensure_data_dir()) {
        return false;
    }

    $path = pkg_users_path();
    $json = json_encode($users, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
    if (!is_string($json)) {
        return false;
    }

    return file_put_contents($path, $json . "\n", LOCK_EX) !== false;
}

function pkg_current_username(): string {
    $name = $_SESSION['pkg_user'] ?? '';
    if (!is_string($name) || !pkg_valid_username($name)) {
        return '';
    }

    $users = pkg_load_users();
    if (!isset($users['users'][$name]) || !is_array($users['users'][$name])) {
        unset($_SESSION['pkg_user']);
        return '';
    }

    return $name;
}

function pkg_register_user(string $username, string $password, string &$message): bool {
    $username = trim($username);
    if (!pkg_valid_username($username)) {
        $message = 'username must be 3-32 chars: letters, digits, underscore, dot or dash';
        return false;
    }

    if (strlen($password) < 6 || strlen($password) > 256) {
        $message = 'password must be 6-256 chars';
        return false;
    }

    $users = pkg_load_users();
    if (isset($users['users'][$username])) {
        $message = 'user already exists';
        return false;
    }

    $hash = password_hash($password, PASSWORD_DEFAULT);
    if (!is_string($hash)) {
        $message = 'password hash failed';
        return false;
    }

    $users['users'][$username] = [
        'password' => $hash,
        'created_at' => gmdate('c'),
    ];

    if (!pkg_save_users($users)) {
        $message = 'failed to save user database';
        return false;
    }

    session_regenerate_id(true);
    $_SESSION['pkg_user'] = $username;
    $message = 'registered and logged in';
    return true;
}

function pkg_login_user(string $username, string $password, string &$message): bool {
    $username = trim($username);
    $users = pkg_load_users();
    $user = $users['users'][$username] ?? null;
    if (!pkg_valid_username($username) || !is_array($user) || !isset($user['password']) ||
        !is_string($user['password']) || !password_verify($password, $user['password'])) {
        $message = 'invalid username or password';
        return false;
    }

    session_regenerate_id(true);
    $_SESSION['pkg_user'] = $username;
    $message = 'logged in';
    return true;
}

function pkg_logout_user(): void {
    unset($_SESSION['pkg_user']);
    session_regenerate_id(true);
}

function pkg_read_meta(string $dir): array {
    $meta = [
        'version' => '1.0.0',
        'target' => '',
        'description' => '',
        'owner' => '',
        'uploaded_at' => '',
    ];
    $path = $dir . '/package.ini';
    if (is_file($path)) {
        $ini = parse_ini_file($path, false, INI_SCANNER_RAW);
        if (is_array($ini)) {
            foreach (array_keys($meta) as $key) {
                if (isset($ini[$key]) && is_string($ini[$key])) {
                    $meta[$key] = trim($ini[$key]);
                }
            }
        }
    }
    return $meta;
}

function pkg_package_info(string $repoDir, string $name): ?array {
    if (!pkg_valid_name($name)) {
        return null;
    }

    $dir = pkg_package_dir($repoDir, $name);
    $elf = $dir . '/' . $name . '.elf';
    if (!is_file($elf)) {
        return null;
    }

    $meta = pkg_read_meta($dir);
    $target = $meta['target'] !== '' ? $meta['target'] : '/shell/' . $name . '.elf';
    $mtime = filemtime($elf);
    $size = filesize($elf);

    return [
        'name' => $name,
        'version' => $meta['version'] !== '' ? $meta['version'] : '1.0.0',
        'target' => $target,
        'description' => $meta['description'],
        'owner' => $meta['owner'],
        'uploaded_at' => $meta['uploaded_at'],
        'size' => is_int($size) ? $size : 0,
        'updated_at' => gmdate('c', is_int($mtime) ? $mtime : time()),
        'manifest_url' => pkg_base_url() . '?manifest=' . rawurlencode($name),
        'download_url' => pkg_base_url() . '?download=' . rawurlencode($name),
    ];
}

function pkg_list_packages(string $repoDir): array {
    $packages = [];
    if (!is_dir($repoDir)) {
        return [];
    }

    foreach (scandir($repoDir) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..' || !pkg_valid_name($entry)) {
            continue;
        }
        $info = pkg_package_info($repoDir, $entry);
        if ($info !== null) {
            $packages[] = $info;
        }
    }

    usort($packages, 'pkg_package_sort_by_name');
    return $packages;
}

function pkg_package_sort_by_name(array $a, array $b): int {
    return strcmp((string)$a['name'], (string)$b['name']);
}

function pkg_search_packages(string $repoDir, string $query): array {
    $query = strtolower(pkg_clean_line($query, 128));
    if ($query === '') {
        return pkg_list_packages($repoDir);
    }

    $matches = [];
    foreach (pkg_list_packages($repoDir) as $package) {
        $haystack = strtolower((string)$package['name'] . ' ' . (string)$package['description'] . ' ' .
                               (string)$package['version'] . ' ' . (string)$package['owner']);
        if (pkg_contains($haystack, $query)) {
            $matches[] = $package;
        }
    }
    return $matches;
}

function pkg_manifest(string $repoDir, string $name): never {
    if (!pkg_valid_name($name)) {
        pkg_not_found('invalid package name');
    }

    $info = pkg_package_info($repoDir, $name);
    if ($info === null) {
        pkg_not_found('package not found');
    }

    pkg_header_text();
    echo "format=cleonos-pkg-v1\n";
    echo "name=" . $name . "\n";
    echo "version=" . $info['version'] . "\n";
    echo "target=" . $info['target'] . "\n";
    echo "url=" . $info['download_url'] . "\n";
    if ($info['description'] !== '') {
        echo "description=" . str_replace(["\r", "\n"], ' ', (string)$info['description']) . "\n";
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

function pkg_target_is_allowed(string $target): bool {
    if ($target === '' || strlen($target) > 160) {
        return false;
    }
    if (!pkg_has_prefix($target, '/shell/') || !pkg_has_suffix($target, '.elf')) {
        return false;
    }
    if (pkg_contains($target, '/../') || pkg_contains($target, '../') || pkg_contains($target, '/..')) {
        return false;
    }
    return preg_match('/^[A-Za-z0-9_\.\-\/]+$/', $target) === 1;
}

function pkg_uploaded_file_has_elf_magic(string $path): bool {
    $fh = fopen($path, 'rb');
    if ($fh === false) {
        return false;
    }
    $magic = fread($fh, 4);
    fclose($fh);
    return $magic === "\x7FELF";
}

function pkg_ini_escape(string $value): string {
    $value = str_replace(["\\", "\"", "\r", "\n"], ["\\\\", "\\\"", ' ', ' '], $value);
    return '"' . $value . '"';
}

function pkg_write_package_meta(string $dir, array $meta): bool {
    $lines = [];
    foreach (['version', 'target', 'description', 'owner', 'uploaded_at'] as $key) {
        $value = isset($meta[$key]) && is_string($meta[$key]) ? $meta[$key] : '';
        $lines[] = $key . '=' . pkg_ini_escape($value);
    }
    return file_put_contents($dir . '/package.ini', implode("\n", $lines) . "\n", LOCK_EX) !== false;
}

function pkg_upload_package(string $repoDir, string $username, array &$outPackage, string &$message): bool {
    $outPackage = [];
    if ($username === '') {
        $message = 'login required';
        return false;
    }

    $name = pkg_clean_line((string)($_POST['name'] ?? ''), 63);
    $version = pkg_clean_line((string)($_POST['version'] ?? ''), 64);
    $target = pkg_clean_line((string)($_POST['target'] ?? ''), 160);
    $description = pkg_clean_line((string)($_POST['description'] ?? ''), 512);

    if (!pkg_valid_name($name)) {
        $message = 'invalid package name';
        return false;
    }
    if ($version === '') {
        $version = '1.0.0';
    }
    if (!pkg_valid_version($version)) {
        $message = 'invalid version';
        return false;
    }
    if ($target === '') {
        $target = '/shell/' . $name . '.elf';
    }
    if (!pkg_target_is_allowed($target)) {
        $message = 'target must be a safe /shell/*.elf path';
        return false;
    }

    $file = $_FILES['elf'] ?? null;
    if (!is_array($file) || !isset($file['error'], $file['tmp_name'], $file['size'])) {
        $message = 'missing ELF upload';
        return false;
    }
    if ((int)$file['error'] !== UPLOAD_ERR_OK) {
        $message = 'upload failed with code ' . (string)(int)$file['error'];
        return false;
    }
    if ((int)$file['size'] <= 0 || (int)$file['size'] > PKG_MAX_UPLOAD_BYTES) {
        $message = 'ELF size must be 1 byte to ' . (string)PKG_MAX_UPLOAD_BYTES . ' bytes';
        return false;
    }

    $tmpName = (string)$file['tmp_name'];
    if (!is_file($tmpName) || !pkg_uploaded_file_has_elf_magic($tmpName)) {
        $message = 'uploaded file is not an ELF';
        return false;
    }

    $existing = pkg_package_info($repoDir, $name);
    if ($existing !== null) {
        $owner = (string)($existing['owner'] ?? '');
        if ($owner === '' || $owner !== $username) {
            $message = 'package already exists and is not owned by current user';
            return false;
        }
    }

    if (!is_dir($repoDir) && !mkdir($repoDir, 0775, true)) {
        $message = 'failed to create package repository directory';
        return false;
    }

    $dir = pkg_package_dir($repoDir, $name);
    if (!is_dir($dir) && !mkdir($dir, 0775, true)) {
        $message = 'failed to create package directory';
        return false;
    }

    $elfPath = $dir . '/' . $name . '.elf';
    $moved = false;
    if (is_uploaded_file($tmpName)) {
        $moved = move_uploaded_file($tmpName, $elfPath);
    } else {
        $moved = rename($tmpName, $elfPath);
        if (!$moved) {
            $moved = copy($tmpName, $elfPath);
        }
    }
    if (!$moved) {
        $message = 'failed to store uploaded ELF';
        return false;
    }
    @chmod($elfPath, 0644);

    $meta = [
        'version' => $version,
        'target' => $target,
        'description' => $description,
        'owner' => $username,
        'uploaded_at' => gmdate('c'),
    ];
    if (!pkg_write_package_meta($dir, $meta)) {
        $message = 'failed to write package metadata';
        return false;
    }

    $info = pkg_package_info($repoDir, $name);
    if ($info !== null) {
        $outPackage = $info;
    }
    $message = 'package uploaded';
    return true;
}

function pkg_api(string $repoDir, string $api): never {
    $api = strtolower(trim($api));

    if ($api === 'list') {
        $packages = pkg_list_packages($repoDir);
        pkg_json([
            'ok' => true,
            'count' => count($packages),
            'packages' => $packages,
        ]);
    }

    if ($api === 'info') {
        $name = (string)($_GET['name'] ?? '');
        $info = pkg_package_info($repoDir, $name);
        if ($info === null) {
            pkg_error_json('package not found', 404);
        }
        pkg_json([
            'ok' => true,
            'package' => $info,
        ]);
    }

    if ($api === 'search') {
        $query = (string)($_GET['q'] ?? '');
        $packages = pkg_search_packages($repoDir, $query);
        pkg_json([
            'ok' => true,
            'q' => pkg_clean_line($query, 128),
            'count' => count($packages),
            'packages' => $packages,
        ]);
    }

    if ($api === 'me') {
        $username = pkg_current_username();
        pkg_json([
            'ok' => true,
            'authenticated' => $username !== '',
            'user' => $username,
        ]);
    }

    if ($api === 'register') {
        if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
            pkg_error_json('POST required', 405);
        }
        $message = '';
        $ok = pkg_register_user((string)($_POST['username'] ?? ''), (string)($_POST['password'] ?? ''), $message);
        if (!$ok) {
            pkg_error_json($message, 400);
        }
        pkg_json([
            'ok' => true,
            'message' => $message,
            'user' => pkg_current_username(),
        ]);
    }

    if ($api === 'login') {
        if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
            pkg_error_json('POST required', 405);
        }
        $message = '';
        $ok = pkg_login_user((string)($_POST['username'] ?? ''), (string)($_POST['password'] ?? ''), $message);
        if (!$ok) {
            pkg_error_json($message, 401);
        }
        pkg_json([
            'ok' => true,
            'message' => $message,
            'user' => pkg_current_username(),
        ]);
    }

    if ($api === 'logout') {
        pkg_logout_user();
        pkg_json([
            'ok' => true,
            'message' => 'logged out',
        ]);
    }

    if ($api === 'upload') {
        if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
            pkg_error_json('POST required', 405);
        }
        $message = '';
        $package = [];
        $ok = pkg_upload_package($repoDir, pkg_current_username(), $package, $message);
        if (!$ok) {
            pkg_error_json($message, 400);
        }
        pkg_json([
            'ok' => true,
            'message' => $message,
            'package' => $package,
        ]);
    }

    pkg_error_json('unknown api', 404);
}

if (isset($_GET['api'])) {
    pkg_api($repoDir, (string)$_GET['api']);
}

if (isset($_GET['manifest'])) {
    pkg_manifest($repoDir, (string)$_GET['manifest']);
}

if (isset($_GET['download'])) {
    pkg_download($repoDir, (string)$_GET['download']);
}

$message = '';
$messageOk = false;
$view = 'home';
if (isset($_GET['register'])) {
    $view = 'register';
} elseif (isset($_GET['login'])) {
    $view = 'login';
} elseif (isset($_GET['upload'])) {
    $view = 'upload';
}

if (isset($_GET['logout'])) {
    pkg_logout_user();
    header('Location: ' . pkg_base_url());
    exit;
}

if ($view === 'register' && ($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $messageOk = pkg_register_user((string)($_POST['username'] ?? ''), (string)($_POST['password'] ?? ''), $message);
}

if ($view === 'login' && ($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $messageOk = pkg_login_user((string)($_POST['username'] ?? ''), (string)($_POST['password'] ?? ''), $message);
}

if ($view === 'upload' && ($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $package = [];
    $messageOk = pkg_upload_package($repoDir, pkg_current_username(), $package, $message);
}

$currentUser = pkg_current_username();
$packages = pkg_list_packages($repoDir);
?><!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>CLeonOS package repository</title>
</head>
<body>
<h1>CLeonOS package repository</h1>
<p>
<a href="<?php echo pkg_html(pkg_base_url()); ?>">home</a>
-
<a href="?register=1">register</a>
-
<?php if ($currentUser === ''): ?>
<a href="?login=1">login</a>
<?php else: ?>
logged in as <?php echo pkg_html($currentUser); ?>
-
<a href="?logout=1">logout</a>
-
<a href="?upload=1">upload package</a>
<?php endif; ?>
</p>
<?php if ($message !== ''): ?>
<p><strong><?php echo $messageOk ? 'OK' : 'Error'; ?>:</strong> <?php echo pkg_html($message); ?></p>
<?php endif; ?>
<?php if ($view === 'register'): ?>
<h2>Register</h2>
<form method="post" action="?register=1">
<p><label>Username <input name="username" required minlength="3" maxlength="32"></label></p>
<p><label>Password <input name="password" type="password" required minlength="6" maxlength="256"></label></p>
<p><button type="submit">Register</button></p>
</form>
<?php elseif ($view === 'login'): ?>
<h2>Login</h2>
<form method="post" action="?login=1">
<p><label>Username <input name="username" required minlength="3" maxlength="32"></label></p>
<p><label>Password <input name="password" type="password" required minlength="6" maxlength="256"></label></p>
<p><button type="submit">Login</button></p>
</form>
<?php elseif ($view === 'upload'): ?>
<h2>Upload Package</h2>
<?php if ($currentUser === ''): ?>
<p>Login required.</p>
<?php else: ?>
<form method="post" action="?upload=1" enctype="multipart/form-data">
<p><label>Name <input name="name" required maxlength="63"></label></p>
<p><label>Version <input name="version" value="1.0.0" required maxlength="64"></label></p>
<p><label>Target <input name="target" placeholder="/shell/name.elf" maxlength="160"></label></p>
<p><label>Description <input name="description" maxlength="512"></label></p>
<p><label>ELF file <input name="elf" type="file" required></label></p>
<p><button type="submit">Upload</button></p>
</form>
<?php endif; ?>
<?php else: ?>
<p>Client usage: pkg repo <?php echo pkg_html(pkg_base_url()); ?></p>
<p>Install example: pkg install hello</p>
<h2>API</h2>
<ul>
<li><a href="?api=list">?api=list</a></li>
<li><a href="?api=info&amp;name=hello">?api=info&amp;name=hello</a></li>
<li><a href="?api=search&amp;q=hello">?api=search&amp;q=hello</a></li>
<li><a href="?api=me">?api=me</a></li>
</ul>
<h2>Packages</h2>
<?php if (count($packages) === 0): ?>
<p>No packages.</p>
<?php else: ?>
<table border="1" cellpadding="4" cellspacing="0">
<tr><th>Name</th><th>Version</th><th>Owner</th><th>Description</th><th>Links</th></tr>
<?php foreach ($packages as $package): ?>
<tr>
<td><?php echo pkg_html((string)$package['name']); ?></td>
<td><?php echo pkg_html((string)$package['version']); ?></td>
<td><?php echo pkg_html((string)$package['owner']); ?></td>
<td><?php echo pkg_html((string)$package['description']); ?></td>
<td>
<a href="?api=info&amp;name=<?php echo rawurlencode((string)$package['name']); ?>">info</a>
-
<a href="?manifest=<?php echo rawurlencode((string)$package['name']); ?>">manifest</a>
-
<a href="?download=<?php echo rawurlencode((string)$package['name']); ?>">download</a>
</td>
</tr>
<?php endforeach; ?>
</table>
<?php endif; ?>
<?php endif; ?>
</body>
</html>
