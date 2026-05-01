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

function pkg_valid_depends(string $depends): bool {
    if ($depends === '') {
        return true;
    }
    if (strlen($depends) > 512 || preg_match('/^[A-Za-z0-9_.:+~<>=!@, -]+$/', $depends) !== 1) {
        return false;
    }

    foreach (explode(',', $depends) as $dep) {
        $dep = trim($dep);
        if ($dep === '') {
            return false;
        }
        if (preg_match('/^([A-Za-z0-9_.-]{1,63})(?:\s*(@|=|==|!=|>=|<=|>|<)\s*([A-Za-z0-9_.:+~-]{1,64}))?$/', $dep) !== 1) {
            return false;
        }
    }
    return true;
}

function pkg_valid_category(string $category): bool {
    return $category === '' || preg_match('/^[A-Za-z0-9_.-]{1,63}$/', $category) === 1;
}

function pkg_valid_tags(string $tags): bool {
    if ($tags === '') {
        return true;
    }
    if (strlen($tags) > 256 || preg_match('/^[A-Za-z0-9_. ,+-]+$/', $tags) !== 1) {
        return false;
    }

    foreach (explode(',', $tags) as $tag) {
        $tag = trim($tag);
        if ($tag === '' || preg_match('/^[A-Za-z0-9_.+-]{1,32}$/', $tag) !== 1) {
            return false;
        }
    }
    return true;
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
        'depends' => '',
        'category' => '',
        'tags' => '',
        'owner' => '',
        'uploaded_at' => '',
        'updated_at' => '',
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
    $updatedAt = $meta['updated_at'] !== '' ? $meta['updated_at'] : gmdate('c', is_int($mtime) ? $mtime : time());

    return [
        'name' => $name,
        'version' => $meta['version'] !== '' ? $meta['version'] : '1.0.0',
        'target' => $target,
        'description' => $meta['description'],
        'depends' => $meta['depends'],
        'category' => $meta['category'],
        'tags' => $meta['tags'],
        'owner' => $meta['owner'],
        'uploaded_at' => $meta['uploaded_at'],
        'size' => is_int($size) ? $size : 0,
        'updated_at' => $updatedAt,
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
                               (string)$package['version'] . ' ' . (string)$package['owner'] . ' ' .
                               (string)$package['depends'] . ' ' . (string)$package['category'] . ' ' .
                               (string)$package['tags']);
        if (pkg_contains($haystack, $query)) {
            $matches[] = $package;
        }
    }
    return $matches;
}

function pkg_filter_category_packages(string $repoDir, string $category): array {
    $category = strtolower(pkg_clean_line($category, 63));
    $matches = [];
    foreach (pkg_list_packages($repoDir) as $package) {
        if (strtolower((string)$package['category']) === $category) {
            $matches[] = $package;
        }
    }
    return $matches;
}

function pkg_filter_tag_packages(string $repoDir, string $tag): array {
    $tag = strtolower(pkg_clean_line($tag, 32));
    $matches = [];
    foreach (pkg_list_packages($repoDir) as $package) {
        foreach (explode(',', (string)$package['tags']) as $item) {
            if (strtolower(trim($item)) === $tag) {
                $matches[] = $package;
                break;
            }
        }
    }
    return $matches;
}

function pkg_list_user_packages(string $repoDir, string $username): array {
    $matches = [];
    if ($username === '') {
        return [];
    }
    foreach (pkg_list_packages($repoDir) as $package) {
        if (isset($package['owner']) && is_string($package['owner']) && $package['owner'] === $username) {
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
    if ($info['depends'] !== '') {
        echo "depends=" . str_replace(["\r", "\n"], ' ', (string)$info['depends']) . "\n";
    }
    if ($info['category'] !== '') {
        echo "category=" . str_replace(["\r", "\n"], ' ', (string)$info['category']) . "\n";
    }
    if ($info['tags'] !== '') {
        echo "tags=" . str_replace(["\r", "\n"], ' ', (string)$info['tags']) . "\n";
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
    foreach (['version', 'target', 'description', 'depends', 'category', 'tags', 'owner', 'uploaded_at', 'updated_at'] as $key) {
        $value = isset($meta[$key]) && is_string($meta[$key]) ? $meta[$key] : '';
        $lines[] = $key . '=' . pkg_ini_escape($value);
    }
    return file_put_contents($dir . '/package.ini', implode("\n", $lines) . "\n", LOCK_EX) !== false;
}

function pkg_user_can_edit_package(?array $info, string $username): bool {
    if ($info === null || $username === '') {
        return false;
    }
    return isset($info['owner']) && is_string($info['owner']) && $info['owner'] === $username;
}

function pkg_post_field(string $key, string $current, int $maxLen): string {
    if (array_key_exists($key, $_POST)) {
        return pkg_clean_line((string)$_POST[$key], $maxLen);
    }
    return $current;
}

function pkg_store_uploaded_elf(array $file, string $elfPath, string &$message): bool {
    if (!isset($file['error'], $file['tmp_name'], $file['size'])) {
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
    return true;
}

function pkg_validate_package_meta(string $name, string $version, string $target, string $depends, string $category,
                                   string $tags, string &$message): bool {
    if (!pkg_valid_name($name)) {
        $message = 'invalid package name';
        return false;
    }
    if ($version === '' || !pkg_valid_version($version)) {
        $message = 'invalid version';
        return false;
    }
    if (!pkg_target_is_allowed($target)) {
        $message = 'target must be a safe /shell/*.elf path';
        return false;
    }
    if (!pkg_valid_depends($depends)) {
        $message = 'invalid dependency list';
        return false;
    }
    if (!pkg_valid_category($category)) {
        $message = 'invalid category';
        return false;
    }
    if (!pkg_valid_tags($tags)) {
        $message = 'invalid tags';
        return false;
    }
    return true;
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
    $depends = pkg_clean_line((string)($_POST['depends'] ?? ''), 512);
    $category = pkg_clean_line((string)($_POST['category'] ?? ''), 63);
    $tags = pkg_clean_line((string)($_POST['tags'] ?? ''), 256);

    if ($version === '') {
        $version = '1.0.0';
    }
    if ($target === '') {
        $target = '/shell/' . $name . '.elf';
    }

    if (!pkg_validate_package_meta($name, $version, $target, $depends, $category, $tags, $message)) {
        return false;
    }

    $file = $_FILES['elf'] ?? null;
    if (!is_array($file)) {
        $message = 'missing ELF upload';
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
    if (!pkg_store_uploaded_elf($file, $elfPath, $message)) {
        return false;
    }

    $now = gmdate('c');
    $uploadedAt = ($existing !== null && isset($existing['uploaded_at']) && is_string($existing['uploaded_at']) &&
                   $existing['uploaded_at'] !== '')
                      ? $existing['uploaded_at']
                      : $now;

    $meta = [
        'version' => $version,
        'target' => $target,
        'description' => $description,
        'depends' => $depends,
        'category' => $category,
        'tags' => $tags,
        'owner' => $username,
        'uploaded_at' => $uploadedAt,
        'updated_at' => $now,
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

function pkg_update_package(string $repoDir, string $username, array &$outPackage, string &$message): bool {
    $outPackage = [];
    if ($username === '') {
        $message = 'login required';
        return false;
    }

    $name = pkg_clean_line((string)($_POST['name'] ?? $_GET['name'] ?? ''), 63);
    if (!pkg_valid_name($name)) {
        $message = 'invalid package name';
        return false;
    }

    $info = pkg_package_info($repoDir, $name);
    if ($info === null) {
        $message = 'package not found';
        return false;
    }
    if (!pkg_user_can_edit_package($info, $username)) {
        $message = 'only the package owner can update this package';
        return false;
    }

    $version = pkg_post_field('version', (string)$info['version'], 64);
    $target = pkg_post_field('target', (string)$info['target'], 160);
    $description = pkg_post_field('description', (string)$info['description'], 512);
    $depends = pkg_post_field('depends', (string)$info['depends'], 512);
    $category = pkg_post_field('category', (string)$info['category'], 63);
    $tags = pkg_post_field('tags', (string)$info['tags'], 256);
    if ($target === '') {
        $target = '/shell/' . $name . '.elf';
    }

    if (!pkg_validate_package_meta($name, $version, $target, $depends, $category, $tags, $message)) {
        return false;
    }

    $dir = pkg_package_dir($repoDir, $name);
    $elfPath = $dir . '/' . $name . '.elf';
    $file = $_FILES['elf'] ?? null;
    if (is_array($file) && isset($file['error']) && (int)$file['error'] !== UPLOAD_ERR_NO_FILE) {
        if (!pkg_store_uploaded_elf($file, $elfPath, $message)) {
            return false;
        }
    }

    $now = gmdate('c');
    $meta = [
        'version' => $version,
        'target' => $target,
        'description' => $description,
        'depends' => $depends,
        'category' => $category,
        'tags' => $tags,
        'owner' => $username,
        'uploaded_at' => (isset($info['uploaded_at']) && is_string($info['uploaded_at'])) ? $info['uploaded_at'] : '',
        'updated_at' => $now,
    ];
    if (!pkg_write_package_meta($dir, $meta)) {
        $message = 'failed to write package metadata';
        return false;
    }

    $updated = pkg_package_info($repoDir, $name);
    if ($updated !== null) {
        $outPackage = $updated;
    }
    $message = 'package updated';
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

    if ($api === 'category') {
        $name = (string)($_GET['name'] ?? '');
        $packages = pkg_filter_category_packages($repoDir, $name);
        pkg_json([
            'ok' => true,
            'category' => pkg_clean_line($name, 63),
            'count' => count($packages),
            'packages' => $packages,
        ]);
    }

    if ($api === 'tag') {
        $name = (string)($_GET['name'] ?? '');
        $packages = pkg_filter_tag_packages($repoDir, $name);
        pkg_json([
            'ok' => true,
            'tag' => pkg_clean_line($name, 32),
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

    if ($api === 'update') {
        if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
            pkg_error_json('POST required', 405);
        }
        $message = '';
        $package = [];
        $ok = pkg_update_package($repoDir, pkg_current_username(), $package, $message);
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
} elseif (isset($_GET['edit'])) {
    $view = 'edit';
} elseif (isset($_GET['mine'])) {
    $view = 'mine';
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

if ($view === 'edit' && ($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $package = [];
    $messageOk = pkg_update_package($repoDir, pkg_current_username(), $package, $message);
}

$currentUser = pkg_current_username();
$packages = pkg_list_packages($repoDir);
$editPackageName = isset($_GET['edit']) ? pkg_clean_line((string)$_GET['edit'], 63) : '';
$editPackage = ($editPackageName !== '') ? pkg_package_info($repoDir, $editPackageName) : null;
$myPackages = ($currentUser !== '') ? pkg_list_user_packages($repoDir, $currentUser) : [];
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
-
<a href="?mine=1">my packages</a>
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
<p><label>Depends <input name="depends" placeholder="foo>=1.0.0,bar" maxlength="512"></label></p>
<p><label>Category <input name="category" placeholder="network" maxlength="63"></label></p>
<p><label>Tags <input name="tags" placeholder="gui,http,tool" maxlength="256"></label></p>
<p><label>ELF file <input name="elf" type="file" required></label></p>
<p><button type="submit">Upload</button></p>
</form>
<?php endif; ?>
<?php elseif ($view === 'edit'): ?>
<h2>Edit Package</h2>
<?php if ($currentUser === ''): ?>
<p>Login required.</p>
<?php elseif ($editPackage === null): ?>
<p>Package not found.</p>
<?php elseif (!pkg_user_can_edit_package($editPackage, $currentUser)): ?>
<p>Only the package owner can edit this package.</p>
<?php else: ?>
<form method="post" action="?edit=<?php echo rawurlencode((string)$editPackage['name']); ?>" enctype="multipart/form-data">
<p><label>Name <input name="name" value="<?php echo pkg_html((string)$editPackage['name']); ?>" readonly></label></p>
<p><label>Version <input name="version" value="<?php echo pkg_html((string)$editPackage['version']); ?>" required maxlength="64"></label></p>
<p><label>Target <input name="target" value="<?php echo pkg_html((string)$editPackage['target']); ?>" maxlength="160"></label></p>
<p><label>Description <input name="description" value="<?php echo pkg_html((string)$editPackage['description']); ?>" maxlength="512"></label></p>
<p><label>Depends <input name="depends" value="<?php echo pkg_html((string)$editPackage['depends']); ?>" maxlength="512"></label></p>
<p><label>Category <input name="category" value="<?php echo pkg_html((string)$editPackage['category']); ?>" maxlength="63"></label></p>
<p><label>Tags <input name="tags" value="<?php echo pkg_html((string)$editPackage['tags']); ?>" maxlength="256"></label></p>
<p><label>Replace ELF <input name="elf" type="file"></label></p>
<p><button type="submit">Update Package</button></p>
</form>
<?php endif; ?>
<?php elseif ($view === 'mine'): ?>
<h2>My Packages</h2>
<?php if ($currentUser === ''): ?>
<p>Login required.</p>
<?php elseif (count($myPackages) === 0): ?>
<p>No packages owned by this account.</p>
<?php else: ?>
<table border="1" cellpadding="4" cellspacing="0">
<tr><th>Name</th><th>Version</th><th>Category</th><th>Tags</th><th>Description</th><th>Links</th></tr>
<?php foreach ($myPackages as $package): ?>
<tr>
<td><?php echo pkg_html((string)$package['name']); ?></td>
<td><?php echo pkg_html((string)$package['version']); ?></td>
<td><?php echo pkg_html((string)$package['category']); ?></td>
<td><?php echo pkg_html((string)$package['tags']); ?></td>
<td><?php echo pkg_html((string)$package['description']); ?></td>
<td>
<a href="?edit=<?php echo rawurlencode((string)$package['name']); ?>">edit</a>
-
<a href="?api=info&amp;name=<?php echo rawurlencode((string)$package['name']); ?>">info</a>
-
<a href="?download=<?php echo rawurlencode((string)$package['name']); ?>">download</a>
</td>
</tr>
<?php endforeach; ?>
</table>
<?php endif; ?>
<?php else: ?>
<p>Client usage: pkg repo <?php echo pkg_html(pkg_base_url()); ?></p>
<p>Install example: pkg install hello</p>
<h2>API</h2>
<ul>
<li><a href="?api=list">?api=list</a></li>
<li><a href="?api=info&amp;name=hello">?api=info&amp;name=hello</a></li>
<li><a href="?api=search&amp;q=hello">?api=search&amp;q=hello</a></li>
<li><a href="?api=category&amp;name=network">?api=category&amp;name=network</a></li>
<li><a href="?api=tag&amp;name=gui">?api=tag&amp;name=gui</a></li>
<li><a href="?api=me">?api=me</a></li>
<li>POST ?api=update</li>
</ul>
<h2>Packages</h2>
<?php if (count($packages) === 0): ?>
<p>No packages.</p>
<?php else: ?>
<table border="1" cellpadding="4" cellspacing="0">
<tr><th>Name</th><th>Version</th><th>Category</th><th>Tags</th><th>Owner</th><th>Description</th><th>Links</th></tr>
<?php foreach ($packages as $package): ?>
<tr>
<td><?php echo pkg_html((string)$package['name']); ?></td>
<td><?php echo pkg_html((string)$package['version']); ?></td>
<td><?php echo pkg_html((string)$package['category']); ?></td>
<td><?php echo pkg_html((string)$package['tags']); ?></td>
<td><?php echo pkg_html((string)$package['owner']); ?></td>
<td><?php echo pkg_html((string)$package['description']); ?></td>
<td>
<a href="?api=info&amp;name=<?php echo rawurlencode((string)$package['name']); ?>">info</a>
-
<a href="?manifest=<?php echo rawurlencode((string)$package['name']); ?>">manifest</a>
-
<a href="?download=<?php echo rawurlencode((string)$package['name']); ?>">download</a>
<?php if ($currentUser !== '' && pkg_user_can_edit_package($package, $currentUser)): ?>
-
<a href="?edit=<?php echo rawurlencode((string)$package['name']); ?>">edit</a>
<?php endif; ?>
</td>
</tr>
<?php endforeach; ?>
</table>
<?php endif; ?>
<?php endif; ?>
</body>
</html>
