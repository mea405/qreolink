# qreolink (skeleton)

Minimal Linux desktop client for Reolink cameras using Qt + libmpv.

**Дисклеймер:** проект **неофициальный** и не связан с Reolink Technology, не одобрен и не поддерживается ею. Название «Reolink» — товарный знак соответствующих правообладателей.

**Disclaimer:** this project is **unofficial** and is not affiliated with, endorsed by, or sponsored by Reolink Technology. “Reolink” is a trademark of its respective owners.

## Описание (RU)

`qreolink` — это легкий desktop-клиент для Linux, который позволяет удобно смотреть несколько IP-камер Reolink по RTSP. Приложение построено на `Qt` и `libmpv`, поддерживает сетку камер (2x2) для постоянного мониторинга и режим одной камеры в высоком качестве. Для каждой камеры можно задать отдельные параметры подключения (IP, логин, пароль, main/sub stream), быстро переключаться между режимами просмотра и получать диагностические статусы потока прямо в интерфейсе. Программа ориентирована на стабильную локальную работу без облачных сервисов и лишней нагрузки.

## Description (EN)

`qreolink` is a lightweight Linux desktop client for convenient viewing of multiple Reolink IP cameras over RTSP. Built with `Qt` and `libmpv`, it supports a 2x2 camera grid for continuous monitoring and a single-camera mode for high-quality viewing. Each camera can be configured individually (IP, username, password, main/sub stream), with quick mode switching and real-time stream diagnostics shown directly in the UI. The app is designed for stable local use without cloud services or unnecessary overhead.

## Features in this skeleton

- 2x2 grid view for 4 cameras (uses sub stream).
- Single camera mode (uses main stream for selected camera).
- Basic camera config loading from `QSettings`.
- In-app settings dialog for editing camera connection fields.

## Build dependencies (Ubuntu/Debian)

```bash
sudo apt install -y cmake ninja-build g++ qt6-base-dev libmpv-dev pkg-config
```

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/qreolink
```

## Build .deb package

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix build/install
cpack --config build/CPackConfig.cmake -G DEB
```

Generated package will be in `build/` (for example: `qreolink-0.1.0-Linux.deb`).

The `.deb` also installs:

- desktop launcher: `/usr/share/applications/qreolink.desktop`
- app icon: `/usr/share/icons/hicolor/scalable/apps/qreolink.svg`

## App icon not visible

If `qreolink` starts but shows a generic icon in app menu or dock, install/update the local launcher and icon cache:

```bash
install -Dm644 packaging/qreolink.desktop ~/.local/share/applications/qreolink.desktop
install -Dm644 assets/qreolink.svg ~/.local/share/icons/hicolor/scalable/apps/qreolink.svg

update-desktop-database ~/.local/share/applications
gtk-update-icon-cache ~/.local/share/icons/hicolor 2>/dev/null || true
```

Then log out and back in. If `qreolink` was pinned in the dock, unpin and pin it again (old launcher entries are often cached).

## Runtime camera config

By default app expects cameras like:

- `192.168.11.226` ... `192.168.11.229`
- username: `admin`
- password: `change-me`

It reads keys from `QSettings` under:

- `cameras/count`
- `cameras/<index>/name`
- `cameras/<index>/host`
- `cameras/<index>/username`
- `cameras/<index>/password`
- `cameras/<index>/mainPath`
- `cameras/<index>/subPath`

## License

This program is licensed under the [GNU General Public License v3.0](LICENSE). Linking against `libmpv` typically requires a GPL-compatible license for the combined work; GPLv3 is used here for clarity.

Программа распространяется на условиях [GNU GPL v3](LICENSE).

## Notes

- This is a starting point; it still needs robust reconnect logic, settings UI,
  and secure credentials storage (for example with QtKeychain).
