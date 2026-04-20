# qreolink Description

## Russian

`qreolink` — это легкий desktop-клиент для Linux, который позволяет удобно смотреть несколько IP-камер Reolink по RTSP. Приложение построено на `Qt` и `libmpv`, поддерживает сетку камер (2x2) для постоянного мониторинга и режим одной камеры в высоком качестве. Для каждой камеры можно задать отдельные параметры подключения (IP, логин, пароль, main/sub stream), быстро переключаться между режимами просмотра и получать диагностические статусы потока прямо в интерфейсе. Программа ориентирована на стабильную локальную работу без облачных сервисов и лишней нагрузки.

## English

`qreolink` is a lightweight Linux desktop client for convenient viewing of multiple Reolink IP cameras over RTSP. Built with `Qt` and `libmpv`, it supports a 2x2 camera grid for continuous monitoring and a single-camera mode for high-quality viewing. Each camera can be configured individually (IP, username, password, main/sub stream), with quick mode switching and real-time stream diagnostics shown directly in the UI. The app is designed for stable local use without cloud services or unnecessary overhead.

## App icon not visible

If `qreolink` starts but shows a generic icon in app menu or dock, install/update the local launcher and icon cache:

```bash
install -Dm644 packaging/qreolink.desktop ~/.local/share/applications/qreolink.desktop
install -Dm644 assets/qreolink.svg ~/.local/share/icons/hicolor/scalable/apps/qreolink.svg

update-desktop-database ~/.local/share/applications
gtk-update-icon-cache ~/.local/share/icons/hicolor 2>/dev/null || true
```

Then log out and back in. If `qreolink` was pinned in the dock, unpin and pin it again (old launcher entries are often cached).
