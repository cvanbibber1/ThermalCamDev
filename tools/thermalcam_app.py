#!/usr/bin/env python3
"""Desktop application for the Lepton 3.1R radiometric thermal camera.

Video arrives over the USB video interface as 160x120 Y16, where every pixel is
an absolute temperature in centikelvin, so any point in the picture can be read
directly. Camera control (flat-field correction, status) runs at the same time
over the USB CDC serial interface.

    python tools/thermalcam_app.py
    python tools/thermalcam_app.py --port COM55 --index 1
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import thermal_imaging as ti

try:
    import cv2
except ImportError:  # pragma: no cover - reported at runtime
    sys.exit("opencv-python is required: pip install opencv-python")

try:
    from PySide6 import QtCore, QtGui, QtWidgets
except ImportError:  # pragma: no cover - reported at runtime
    sys.exit("PySide6 is required: pip install PySide6-Essentials")


class CameraControl:
    """Optional CDC control link. Video works whether or not this connects."""

    RETRY_SECONDS = 2.0

    def __init__(self) -> None:
        self._link = None
        self._cli = None
        self._preferred = None
        self._last_attempt = 0.0
        self.port = None
        self.error = None

    def ensure(self) -> bool:
        """Reconnect if the link dropped.

        The device re-enumerates on every reset and firmware load, which
        invalidates the handle. Without this the application keeps a dead port
        open, which also stops anything else from using it.
        """
        if self._link is not None:
            return True
        now = time.time()
        if (now - self._last_attempt) < self.RETRY_SECONDS:
            return False
        self._last_attempt = now
        return self.connect(self._preferred)

    def _drop(self, reason: str) -> None:
        self.close()
        self.error = reason
        self.port = None

    def connect(self, port: str | None) -> bool:
        self._preferred = port
        try:
            import importlib.util

            import serial.tools.list_ports as list_ports

            location = Path(__file__).resolve().parent / "thermalcam_cli.py"
            spec = importlib.util.spec_from_file_location("thermalcam_cli", str(location))
            cli = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cli)
            self._cli = cli

            if port is None:
                found = [p.device for p in list_ports.comports() if "1209:F412" in p.hwid]
                if not found:
                    self.error = "no camera serial port found"
                    return False
                port = found[0]

            self._link = cli.CameraLink(port, 921600, 1, 2.0)
            self.port = port
            self.error = None
            return True
        except Exception as exc:  # noqa: BLE001 - shown in the status bar
            self.error = str(exc)
            self._link = None
            return False

    @property
    def connected(self) -> bool:
        return self._link is not None

    def _request(self, name: str, payload: bytes = b"") -> bytes | None:
        """One command, dropping the link on failure so it gets retried."""
        if not self.ensure():
            return None
        try:
            return self._link.request(self._cli.OPCODES[name], payload)
        except Exception as exc:  # noqa: BLE001 - shown in the status bar
            self._drop(f"link lost: {exc}")
            return None

    def run_ffc(self) -> str:
        if self._request("ffc") is None:
            return f"flat-field correction failed: {self.error or 'no control link'}"
        return "flat-field correction requested"

    def dosimeter(self) -> dict | None:
        body = self._request("dosimeter")
        return None if body is None else self._cli.decode_dosimeter(body)

    def zero_dosimeter(self) -> str:
        """Capture a new zero reference and persist it in the camera's flash."""
        import struct

        body = self._request("dosimeter-zero")
        if body is None:
            return f"zero failed: {self.error or 'no control link'}"
        samples, = struct.unpack("<I", body[:4])
        return f"zeroing, averaging {samples} samples"

    def ffc_status(self) -> str | None:
        import struct

        body = self._request("ffc-status")
        if body is None:
            return None
        mode, _lock, elapsed, period, _delta, _state = struct.unpack("<IIIIIi", body[:24])
        names = {0: "manual", 1: "auto", 2: "external"}
        return (
            f"shutter {names.get(mode, mode)}, last correction "
            f"{elapsed / 1000:.0f}s ago of {period / 1000:.0f}s"
        )

    def execute(self, text: str) -> str:
        """Run any command-line command over the link this window holds.

        Serial ports are exclusive, so while the application is connected the
        command-line tool cannot reach the camera. Rather than duplicate every
        command here, the same parser and decoders are driven directly and
        their output captured.
        """
        import contextlib
        import io

        words = text.split()
        if not words:
            return ""
        if not self.ensure():
            return f"no control link ({self.error or 'not connected'})"
        try:
            # argparse writes its own usage to stderr and exits; keep both
            # inside the console instead of the terminal the window came from.
            with contextlib.redirect_stderr(io.StringIO()) as usage:
                arguments = self._cli.parse_args(words)
        except SystemExit:
            detail = usage.getvalue().strip().splitlines()
            return detail[-1] if detail else "unrecognised command"
        buffer = io.StringIO()
        try:
            with contextlib.redirect_stdout(buffer):
                self._cli.run(arguments, link=self._link)
        except Exception as exc:  # noqa: BLE001 - reported in the console
            return f"{buffer.getvalue()}error: {exc}"
        return buffer.getvalue() or "ok"

    def close(self) -> None:
        if self._link is not None:
            try:
                self._link.close()
            except Exception:  # noqa: BLE001
                pass
            self._link = None


class FrameGrabber(QtCore.QThread):
    """Reads Y16 frames from the USB video interface.

    DirectShow with colour conversion disabled is the only Windows path that
    returns the raw 16-bit values. Every other backend hands back an 8-bit RGB
    conversion, which discards the temperatures.
    """

    frame_ready = QtCore.Signal(object)
    status = QtCore.Signal(str)
    reconnected = QtCore.Signal()

    """Consecutive bad reads before the device is treated as gone."""
    MISS_LIMIT = 40

    def __init__(self, index: int | None) -> None:
        super().__init__()
        self._index = index
        self._running = True

    @staticmethod
    def _configure(capture) -> None:
        capture.set(cv2.CAP_PROP_CONVERT_RGB, 0)
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, ti.WIDTH)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, ti.HEIGHT)

    @staticmethod
    def _is_y16(frame) -> bool:
        """Whether this is a frame from our camera at all.

        Presence of the device is judged on format only. Content must not come
        into it: the camera sends a blank placeholder for the ten seconds or so
        its sensor takes to boot, and treating that as absence would make the
        application hunt for a camera that is right there.
        """
        return (
            frame is not None
            and frame.dtype == np.uint16
            and frame.shape == (ti.HEIGHT, ti.WIDTH)
        )

    @staticmethod
    def _has_image(frame) -> bool:
        """Whether the frame carries real radiometric data.

        The placeholder, and a frame caught part-written after a reconnect,
        contain zero-valued pixels. Zero is absolute zero in this encoding, so
        such a frame is not measurable data; displaying one reports -273 C and
        stretches the automatic contrast over a range that does not exist.
        """
        return bool(frame.min() > 0)

    @classmethod
    def probe(cls) -> int | None:
        for index in range(8):
            capture = cv2.VideoCapture(index, cv2.CAP_DSHOW)
            if not capture.isOpened():
                capture.release()
                continue
            cls._configure(capture)
            ok, frame = capture.read()
            capture.release()
            if ok and cls._is_y16(frame):
                return index
        return None

    def _wait(self, seconds: float) -> None:
        """Sleep in slices so stop() stays responsive."""
        deadline = time.time() + seconds
        while self._running and time.time() < deadline:
            self.msleep(100)

    def run(self) -> None:
        """Open the camera and keep it open, reconnecting as needed.

        The device disappears and comes back on every reset and firmware load.
        Reopening rather than giving up means the window recovers by itself,
        and more importantly it does not sit on a dead handle that would block
        the next client from using the camera at all.
        """
        while self._running:
            index = self._index if self._index is not None else self.probe()
            if index is None:
                self.status.emit("looking for the camera")
                self._wait(1.5)
                continue

            capture = cv2.VideoCapture(index, cv2.CAP_DSHOW)
            if not capture.isOpened():
                capture.release()
                self.status.emit("camera is busy in another application")
                self._wait(1.5)
                continue
            self._configure(capture)
            self.status.emit("connected")

            misses = 0
            self.reconnected.emit()
            try:
                while self._running:
                    ok, frame = capture.read()
                    if not ok or not self._is_y16(frame):
                        misses += 1
                        if misses >= self.MISS_LIMIT:
                            break
                        continue
                    misses = 0
                    if not self._has_image(frame):
                        # Device is present but its sensor is not imaging yet.
                        # Skip quietly; whether frames are flowing is judged by
                        # the window from the arrival times, so there is no
                        # state here that can be left stale.
                        continue
                    self.frame_ready.emit(frame.astype(np.float64))
            finally:
                capture.release()

            if self._running:
                self.status.emit("camera disconnected, reconnecting")
                self._wait(1.0)

    def stop(self) -> None:
        self._running = False
        self.wait(2000)


@dataclass
class Spot:
    x: int
    y: int


@dataclass
class ViewSettings:
    colormap: str = "ironbow"
    agc: str = "plateau"
    plateau: float = 0.015
    low_pct: float = 0.5
    high_pct: float = 99.5
    manual_low_c: float = 15.0
    manual_high_c: float = 40.0
    sharpen: float = 0.5
    destripe: bool = True
    flip_h: bool = False
    flip_v: bool = False
    unit: str = "C"
    show_extremes: bool = True
    spots: list[Spot] = field(default_factory=list)


class PreviewLabel(QtWidgets.QLabel):
    clicked = QtCore.Signal(int, int)
    hovered = QtCore.Signal(int, int)

    def __init__(self) -> None:
        super().__init__()
        self.setMinimumSize(ti.WIDTH * 3, ti.HEIGHT * 3)
        self.setAlignment(QtCore.Qt.AlignCenter)
        self.setMouseTracking(True)
        self.setCursor(QtCore.Qt.CrossCursor)
        self.setStyleSheet("background:#101014;")
        self._origin_x = 0
        self._origin_y = 0
        self._scale = 0.0

    def set_image_geometry(self, x: int, y: int, scale: float) -> None:
        self._origin_x, self._origin_y, self._scale = x, y, scale

    def _to_sensor(self, point) -> tuple[int, int] | None:
        if self._scale <= 0.0:
            return None
        x = int((point.x() - self._origin_x) / self._scale)
        y = int((point.y() - self._origin_y) / self._scale)
        if 0 <= x < ti.WIDTH and 0 <= y < ti.HEIGHT:
            return x, y
        return None

    def mousePressEvent(self, event) -> None:
        found = self._to_sensor(event.position())
        if found is not None:
            self.clicked.emit(found[0], found[1])

    def mouseMoveEvent(self, event) -> None:
        found = self._to_sensor(event.position())
        if found is not None:
            self.hovered.emit(found[0], found[1])


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, args) -> None:
        super().__init__()
        self.setWindowTitle("Lepton 3.1R Thermal Camera")
        self.settings = ViewSettings()
        self.frame = None
        self.display = None
        self.output_dir = Path(args.output).resolve()
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self._writer = None
        self._raw_log = None
        self._recorded = 0
        self._recording_path = None
        self._times: list[float] = []
        self._unique_times: list[float] = []
        self._previous = None
        self._hover = None
        self._image_buffer = None
        self._link_state = "starting"
        self._restarting = False
        self._abandoned: list = []
        self.dose = None
        self._dose_log: list[dict] = []

        self._build_ui()

        self.control = CameraControl()
        self.control.connect(args.port)

        self._grabber_index = args.index
        self._start_grabber()

        self._status_timer = QtCore.QTimer(self)
        self._status_timer.timeout.connect(self.refresh_status)
        self._status_timer.start(2000)
        self._liveness_timer = QtCore.QTimer(self)
        self._liveness_timer.timeout.connect(self.refresh_liveness)
        self._liveness_timer.start(1000)
        self._dose_timer = QtCore.QTimer(self)
        self._dose_timer.timeout.connect(self.refresh_dose)
        self._dose_timer.start(500)
        self.refresh_status()

    def _build_ui(self) -> None:
        central = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(central)
        self.preview = PreviewLabel()
        self.preview.clicked.connect(self.on_click)
        self.preview.hovered.connect(self.on_hover)
        layout.addWidget(self.preview, 1)
        layout.addWidget(self._build_panel())
        self.setCentralWidget(central)

        self.readout = QtWidgets.QLabel("waiting for frames")
        self.statusBar().addWidget(self.readout, 1)
        self.video_label = QtWidgets.QLabel("starting")
        self.statusBar().addPermanentWidget(self.video_label)
        self.link_label = QtWidgets.QLabel("")
        self.statusBar().addPermanentWidget(self.link_label)

    def _build_panel(self) -> QtWidgets.QWidget:
        panel = QtWidgets.QWidget()
        panel.setFixedWidth(330)
        box = QtWidgets.QVBoxLayout(panel)

        image_group = QtWidgets.QGroupBox("Image")
        form = QtWidgets.QFormLayout(image_group)

        self.colormap_box = QtWidgets.QComboBox()
        self.colormap_box.addItems(list(ti.COLORMAPS))
        self.colormap_box.setCurrentText(self.settings.colormap)
        self.colormap_box.currentTextChanged.connect(self._set_colormap)
        form.addRow("Palette", self.colormap_box)

        self.agc_box = QtWidgets.QComboBox()
        self.agc_box.addItems(["plateau", "linear", "equalize", "manual"])
        self.agc_box.setCurrentText(self.settings.agc)
        self.agc_box.currentTextChanged.connect(self.on_agc_changed)
        form.addRow("Contrast", self.agc_box)

        self.sharpen_slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        self.sharpen_slider.setRange(0, 200)
        self.sharpen_slider.setValue(int(self.settings.sharpen * 100))
        self.sharpen_slider.valueChanged.connect(self._set_sharpen)
        form.addRow("Sharpen", self.sharpen_slider)

        self.destripe_check = QtWidgets.QCheckBox("Remove column noise")
        self.destripe_check.setChecked(self.settings.destripe)
        self.destripe_check.toggled.connect(self._set_destripe)
        form.addRow(self.destripe_check)

        flips = QtWidgets.QHBoxLayout()
        self.flip_h_check = QtWidgets.QCheckBox("Flip H")
        self.flip_h_check.toggled.connect(self._set_flip_h)
        self.flip_v_check = QtWidgets.QCheckBox("Flip V")
        self.flip_v_check.toggled.connect(self._set_flip_v)
        flips.addWidget(self.flip_h_check)
        flips.addWidget(self.flip_v_check)
        form.addRow(flips)
        box.addWidget(image_group)

        self.range_group = QtWidgets.QGroupBox("Manual range")
        range_form = QtWidgets.QFormLayout(self.range_group)
        self.low_spin = QtWidgets.QDoubleSpinBox()
        self.low_spin.setRange(-40.0, 400.0)
        self.low_spin.setValue(self.settings.manual_low_c)
        self.low_spin.setSuffix(" °C")
        self.low_spin.valueChanged.connect(self._set_low)
        self.high_spin = QtWidgets.QDoubleSpinBox()
        self.high_spin.setRange(-40.0, 400.0)
        self.high_spin.setValue(self.settings.manual_high_c)
        self.high_spin.setSuffix(" °C")
        self.high_spin.valueChanged.connect(self._set_high)
        range_form.addRow("Min", self.low_spin)
        range_form.addRow("Max", self.high_spin)
        from_frame = QtWidgets.QPushButton("Set from current frame")
        from_frame.clicked.connect(self.on_range_from_frame)
        range_form.addRow(from_frame)
        self.range_group.setEnabled(False)
        box.addWidget(self.range_group)

        measure_group = QtWidgets.QGroupBox("Measurement")
        measure = QtWidgets.QVBoxLayout(measure_group)
        units = QtWidgets.QHBoxLayout()
        self.celsius_radio = QtWidgets.QRadioButton("°C")
        self.celsius_radio.setChecked(True)
        self.fahrenheit_radio = QtWidgets.QRadioButton("°F")
        self.celsius_radio.toggled.connect(self._set_unit)
        units.addWidget(self.celsius_radio)
        units.addWidget(self.fahrenheit_radio)
        units.addStretch(1)
        measure.addLayout(units)

        self.extremes_check = QtWidgets.QCheckBox("Mark hottest and coldest")
        self.extremes_check.setChecked(self.settings.show_extremes)
        self.extremes_check.toggled.connect(self._set_extremes)
        measure.addWidget(self.extremes_check)

        hint = QtWidgets.QLabel("Click the image to add a spot marker.")
        hint.setWordWrap(True)
        measure.addWidget(hint)
        clear = QtWidgets.QPushButton("Clear spots")
        clear.clicked.connect(self.on_clear_spots)
        measure.addWidget(clear)
        self.spot_list = QtWidgets.QLabel("")
        self.spot_list.setStyleSheet("font-family: Consolas, monospace;")
        measure.addWidget(self.spot_list)
        box.addWidget(measure_group)

        dose_group = QtWidgets.QGroupBox("Dosimeter")
        dose_layout = QtWidgets.QVBoxLayout(dose_group)
        self.dose_label = QtWidgets.QLabel("no reading")
        self.dose_label.setStyleSheet("font-size: 17pt; font-family: Consolas, monospace;")
        dose_layout.addWidget(self.dose_label)
        self.dose_detail = QtWidgets.QLabel("")
        self.dose_detail.setStyleSheet("color: #888;")
        self.dose_detail.setWordWrap(True)
        dose_layout.addWidget(self.dose_detail)
        self.zero_button = QtWidgets.QPushButton("Zero (store in camera flash)")
        self.zero_button.clicked.connect(self.on_zero_dosimeter)
        dose_layout.addWidget(self.zero_button)
        box.addWidget(dose_group)

        camera_group = QtWidgets.QGroupBox("Camera")
        camera = QtWidgets.QVBoxLayout(camera_group)
        self.ffc_button = QtWidgets.QPushButton("Run flat-field correction")
        self.ffc_button.clicked.connect(self.on_ffc)
        camera.addWidget(self.ffc_button)
        box.addWidget(camera_group)

        console_group = QtWidgets.QGroupBox("Command console")
        console_layout = QtWidgets.QVBoxLayout(console_group)
        self.console_input = QtWidgets.QLineEdit()
        self.console_input.setPlaceholderText("health, cci-get 0x4EC0 2, ...")
        self.console_input.returnPressed.connect(self.on_console)
        console_layout.addWidget(self.console_input)
        self.console_output = QtWidgets.QPlainTextEdit()
        self.console_output.setReadOnly(True)
        self.console_output.setMaximumHeight(120)
        self.console_output.setStyleSheet("font-family: Consolas, monospace; font-size: 9pt;")
        console_layout.addWidget(self.console_output)
        box.addWidget(console_group)

        capture_group = QtWidgets.QGroupBox("Capture")
        capture = QtWidgets.QVBoxLayout(capture_group)
        snapshot = QtWidgets.QPushButton("Save image")
        snapshot.clicked.connect(self.on_snapshot)
        capture.addWidget(snapshot)
        self.record_button = QtWidgets.QPushButton("Start recording")
        self.record_button.setCheckable(True)
        self.record_button.toggled.connect(self.on_record)
        capture.addWidget(self.record_button)
        self.raw_check = QtWidgets.QCheckBox("Also log raw Y16 while recording")
        capture.addWidget(self.raw_check)
        open_folder = QtWidgets.QPushButton("Open output folder")
        open_folder.clicked.connect(self.on_open_folder)
        capture.addWidget(open_folder)
        box.addWidget(capture_group)

        box.addStretch(1)
        return panel

    def _set_colormap(self, value: str) -> None:
        self.settings.colormap = value

    def _set_sharpen(self, value: int) -> None:
        self.settings.sharpen = value / 100.0

    def _set_destripe(self, value: bool) -> None:
        self.settings.destripe = value

    def _set_flip_h(self, value: bool) -> None:
        self.settings.flip_h = value

    def _set_flip_v(self, value: bool) -> None:
        self.settings.flip_v = value

    def _set_low(self, value: float) -> None:
        self.settings.manual_low_c = value

    def _set_high(self, value: float) -> None:
        self.settings.manual_high_c = value

    def _set_unit(self, celsius: bool) -> None:
        self.settings.unit = "C" if celsius else "F"
        self._paint()
        self._update_readout()

    def _set_extremes(self, value: bool) -> None:
        self.settings.show_extremes = value

    def _oriented(self, frame):
        if self.settings.flip_v:
            frame = frame[::-1, :]
        if self.settings.flip_h:
            frame = frame[:, ::-1]
        return np.ascontiguousarray(frame)

    def on_frame(self, frame) -> None:
        frame = self._oriented(frame)
        self.frame = frame
        now = time.time()
        self._times.append(now)
        if len(self._times) > 60:
            self._times.pop(0)
        # The video pin delivers about 13 payloads a second but the camera only
        # produces about 9 distinct frames, so report the rate that matters.
        if self._previous is None or not np.array_equal(frame, self._previous):
            self._unique_times.append(now)
            if len(self._unique_times) > 40:
                self._unique_times.pop(0)
        self._previous = frame

        source = ti.destripe(frame) if self.settings.destripe else frame
        mode = self.settings.agc
        if mode == "plateau":
            norm = ti.agc_plateau(source, self.settings.plateau)
        elif mode == "linear":
            norm = ti.agc_linear(source, self.settings.low_pct, self.settings.high_pct)
        elif mode == "equalize":
            norm = ti.agc_equalize(source)
        else:
            norm = ti.agc_manual(
                source,
                ti.celsius_to_counts(self.settings.manual_low_c),
                ti.celsius_to_counts(self.settings.manual_high_c),
            )
        norm = ti.unsharp(norm, self.settings.sharpen)
        self.display = np.ascontiguousarray(ti.colorize(norm, self.settings.colormap))

        if self._writer is not None:
            self._writer.write(cv2.cvtColor(self.display, cv2.COLOR_RGB2BGR))
            self._recorded += 1
            if self._raw_log is not None:
                self._raw_log.write(frame.astype("<u2").tobytes())

        self._paint()
        self._update_readout()

    def _paint(self) -> None:
        if self.display is None:
            return
        # QImage does not copy, so the backing bytes must outlive it.
        self._image_buffer = self.display.tobytes()
        image = QtGui.QImage(
            self._image_buffer, ti.WIDTH, ti.HEIGHT, ti.WIDTH * 3,
            QtGui.QImage.Format_RGB888,
        )
        area = self.preview.size()
        scale = max(1.0, min(area.width() / ti.WIDTH, area.height() / ti.HEIGHT))
        pixmap = QtGui.QPixmap.fromImage(image).scaled(
            QtCore.QSize(int(ti.WIDTH * scale), int(ti.HEIGHT * scale)),
            QtCore.Qt.KeepAspectRatio,
            QtCore.Qt.SmoothTransformation,
        )
        self.preview.set_image_geometry(
            (area.width() - pixmap.width()) // 2,
            (area.height() - pixmap.height()) // 2,
            pixmap.width() / ti.WIDTH,
        )
        self._draw_overlay(pixmap)
        self.preview.setPixmap(pixmap)

    def _draw_overlay(self, pixmap) -> None:
        if self.frame is None:
            return
        scale = pixmap.width() / ti.WIDTH
        painter = QtGui.QPainter(pixmap)
        painter.setRenderHint(QtGui.QPainter.Antialiasing)
        font = painter.font()
        font.setPointSizeF(max(7.0, 2.6 * scale))
        painter.setFont(font)

        if self.settings.show_extremes:
            for index, colour in (
                (int(self.frame.argmin()), "#7fd0ff"),
                (int(self.frame.argmax()), "#ff7043"),
            ):
                y, x = divmod(index, ti.WIDTH)
                self._marker(painter, pixmap, x, y, scale, colour)

        for spot in self.settings.spots:
            self._marker(painter, pixmap, spot.x, spot.y, scale, "#ffffff")
        painter.end()

    def _marker(self, painter, pixmap, x, y, scale, colour) -> None:
        text = ti.format_temperature(self.frame[y, x], self.settings.unit)
        px = (x + 0.5) * scale
        py = (y + 0.5) * scale
        radius = max(4.0, 1.5 * scale)

        pen = QtGui.QPen(QtGui.QColor(colour))
        pen.setWidthF(max(1.4, scale / 5.0))
        painter.setPen(pen)
        painter.drawLine(QtCore.QPointF(px - radius, py), QtCore.QPointF(px + radius, py))
        painter.drawLine(QtCore.QPointF(px, py - radius), QtCore.QPointF(px, py + radius))

        rect = painter.fontMetrics().boundingRect(text).adjusted(-4, -2, 4, 2)
        # Place the label beside the crosshair, flipping to the other side
        # rather than letting it ride the edge of the frame.
        left = px + radius + 3.0
        if left + rect.width() > pixmap.width():
            left = px - radius - 3.0 - rect.width()
        top = py - radius - rect.height()
        if top < 0.0:
            top = py + radius
        left = min(max(left, 0.0), float(pixmap.width() - rect.width()))
        top = min(max(top, 0.0), float(pixmap.height() - rect.height()))
        rect.moveTo(int(left), int(top))
        painter.fillRect(rect, QtGui.QColor(0, 0, 0, 160))
        painter.drawText(rect, QtCore.Qt.AlignCenter, text)

    def on_click(self, x: int, y: int) -> None:
        self.settings.spots.append(Spot(x, y))
        self._paint()
        self._update_readout()

    def on_hover(self, x: int, y: int) -> None:
        self._hover = (x, y)
        self._update_readout()

    def on_clear_spots(self) -> None:
        self.settings.spots.clear()
        self._paint()
        self._update_readout()

    def on_agc_changed(self, value: str) -> None:
        self.settings.agc = value
        self.range_group.setEnabled(value == "manual")

    def on_range_from_frame(self) -> None:
        if self.frame is None:
            return
        self.low_spin.setValue(float(ti.counts_to_celsius(self.frame.min())))
        self.high_spin.setValue(float(ti.counts_to_celsius(self.frame.max())))

    def on_ffc(self) -> None:
        self.statusBar().showMessage(self.control.run_ffc(), 4000)

    def on_open_folder(self) -> None:
        QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(self.output_dir)))

    def _metadata(self) -> dict:
        """Everything needed to interpret a capture later."""
        unit = self.settings.unit
        meta = {
            "captured": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "camera": "FLIR Lepton 3.1R",
            "width": ti.WIDTH,
            "height": ti.HEIGHT,
            "pixel_units": "centikelvin",
            "palette": self.settings.colormap,
            "contrast": self.settings.agc,
        }
        if self.frame is not None:
            meta["scene_min_c"] = round(float(ti.counts_to_celsius(self.frame.min())), 2)
            meta["scene_max_c"] = round(float(ti.counts_to_celsius(self.frame.max())), 2)
            meta["spots"] = [
                {"x": spot.x, "y": spot.y,
                 "temperature": ti.format_temperature(self.frame[spot.y, spot.x], unit)}
                for spot in self.settings.spots
            ]
        if self.dose is not None:
            meta["dose_rad"] = round(self.dose["rad"], 4)
            meta["dosimeter_uv"] = self.dose["filtered_voltage_uv"]
            meta["dosimeter_zero_uv"] = self.dose["zero_uv"]
            meta["dosimeter_flags"] = self.dose["flags"]
        return meta

    def on_snapshot(self) -> None:
        if self.frame is None or self.display is None:
            return
        base = self.output_dir / f"thermal-{time.strftime('%Y%m%d-%H%M%S')}"
        meta = self._metadata()

        # Written through PIL so the dose and scene values travel inside the
        # PNG as text chunks; cv2.imwrite cannot carry metadata.
        try:
            from PIL import Image, PngImagePlugin

            info = PngImagePlugin.PngInfo()
            for key, value in meta.items():
                info.add_text(key, value if isinstance(value, str) else json.dumps(value))
            Image.fromarray(self.display).save(base.with_suffix(".png"), pnginfo=info)
        except Exception:  # noqa: BLE001 - fall back to an image with no metadata
            cv2.imwrite(str(base.with_suffix(".png")),
                        cv2.cvtColor(self.display, cv2.COLOR_RGB2BGR))

        self.frame.astype("<u2").tofile(base.with_suffix(".raw"))
        base.with_suffix(".json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

        # Per-pixel temperatures in a form a spreadsheet opens directly.
        with open(base.with_suffix(".csv"), "w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow([f"# {json.dumps(meta)}"])
            for row in ti.counts_to_celsius(self.frame):
                writer.writerow([f"{value:.2f}" for value in row])

        dose = f", {meta['dose_rad']:+.3f} rad" if "dose_rad" in meta else ""
        self.statusBar().showMessage(f"saved {base.name}{dose}", 5000)

    def on_record(self, active: bool) -> None:
        if active:
            if self.display is None:
                self.record_button.setChecked(False)
                return
            self._recording_path = self.output_dir / f"thermal-{time.strftime('%Y%m%d-%H%M%S')}.mp4"
            # The camera produces roughly nine unique frames a second.
            self._writer = cv2.VideoWriter(
                str(self._recording_path),
                cv2.VideoWriter_fourcc(*"mp4v"),
                9.0,
                (ti.WIDTH, ti.HEIGHT),
            )
            if not self._writer.isOpened():
                self._writer = None
                self.record_button.setChecked(False)
                self.statusBar().showMessage("could not open the video writer", 5000)
                return
            if self.raw_check.isChecked():
                self._raw_log = open(self._recording_path.with_suffix(".y16"), "wb")
            self._recorded = 0
            self._dose_log = []
            self._recording_started = time.strftime("%Y-%m-%dT%H:%M:%S")
            self.record_button.setText("Stop recording")
        else:
            if self._writer is not None:
                self._writer.release()
                self._writer = None
            if self._raw_log is not None:
                self._raw_log.close()
                self._raw_log = None
            if self._recording_path is not None:
                # Video containers cannot carry a dose trace, so it goes beside
                # the file with the frame count needed to line it up.
                meta = self._metadata()
                meta["started"] = getattr(self, "_recording_started", meta["captured"])
                meta["frames"] = self._recorded
                meta["nominal_fps"] = 9.0
                meta["dose_samples"] = self._dose_log
                self._recording_path.with_suffix(".json").write_text(
                    json.dumps(meta, indent=2), encoding="utf-8"
                )
                self.statusBar().showMessage(
                    f"saved {self._recording_path.name}, {self._recorded} frames "
                    f"and {len(self._dose_log)} dose samples", 6000
                )
            self.record_button.setText("Start recording")

    def _update_readout(self) -> None:
        if self.frame is None:
            return
        unit = self.settings.unit
        parts = [
            f"min {ti.format_temperature(self.frame.min(), unit)}",
            f"max {ti.format_temperature(self.frame.max(), unit)}",
            f"centre {ti.format_temperature(self.frame[ti.HEIGHT // 2, ti.WIDTH // 2], unit)}",
        ]
        if self._hover is not None:
            x, y = self._hover
            parts.append(f"cursor {ti.format_temperature(self.frame[y, x], unit)}")
        if len(self._unique_times) > 1:
            span = self._unique_times[-1] - self._unique_times[0]
            if span > 0:
                parts.append(f"{(len(self._unique_times) - 1) / span:.1f} fps")
        if self._writer is not None:
            parts.append(f"recording {self._recorded}")
        self.readout.setText("    ".join(parts))

        self.spot_list.setText(
            "\n".join(
                f"{number}. ({spot.x:3d},{spot.y:3d})  "
                f"{ti.format_temperature(self.frame[spot.y, spot.x], unit)}"
                for number, spot in enumerate(self.settings.spots, start=1)
            )
        )

    def _start_grabber(self) -> None:
        self.grabber = FrameGrabber(self._grabber_index)
        self.grabber.frame_ready.connect(self.on_frame)
        self.grabber.status.connect(self.on_video_status)
        self.grabber.reconnected.connect(self.on_reconnected)
        self.grabber.start()

    def refresh_status(self) -> None:
        if not self.control.connected:
            self.link_label.setText(f"control link: {self.control.error or 'offline'}")
            self.ffc_button.setEnabled(False)
            return
        self.ffc_button.setEnabled(True)
        status = self.control.ffc_status()
        self.link_label.setText(
            f"{self.control.port}    {status}" if status else str(self.control.port)
        )

    def on_console(self) -> None:
        text = self.console_input.text().strip()
        if not text:
            return
        self.console_input.clear()
        self.console_output.appendPlainText(f"> {text}")
        self.console_output.appendPlainText(self.control.execute(text).rstrip())
        self.console_output.verticalScrollBar().setValue(
            self.console_output.verticalScrollBar().maximum()
        )

    def on_zero_dosimeter(self) -> None:
        self.statusBar().showMessage(self.control.zero_dosimeter(), 5000)

    def refresh_dose(self) -> None:
        sample = self.control.dosimeter()
        if sample is None:
            self.zero_button.setEnabled(False)
            self.dose_label.setText("no reading")
            self.dose_detail.setText("control link offline")
            return
        self.zero_button.setEnabled(True)
        self.dose = sample
        if self._writer is not None:
            self._dose_log.append(
                {"t": sample["timestamp_ms"], "rad": sample["rad"],
                 "uv": sample["filtered_voltage_uv"]}
            )

        self.dose_label.setText(f"{sample['rad']:+.3f} rad")
        notes = self._cli_flags(sample["flags"])
        self.dose_detail.setText(
            f"{sample['filtered_voltage_uv']} uV, zero {sample['zero_uv']} uV"
            + (f"\n{notes}" if notes else "")
        )

    @staticmethod
    def _cli_flags(flags: int) -> str:
        names = {0x01: "nominal 157.5 mV intercept", 0x02: "saturated",
                 0x04: "stale", 0x08: "zeroing..."}
        return ", ".join(name for bit, name in names.items() if flags & bit)

    STALL_RESTART_SECONDS = 8.0

    def refresh_liveness(self) -> None:
        """Report the video state, and restart a capture that has stalled.

        When the camera re-enumerates, which happens on every reset and
        firmware load, DirectShow can leave the reading thread blocked inside
        read() on the dead handle. That thread cannot notice it is stuck, so
        the check has to come from here.
        """
        if self._link_state != "connected":
            return
        if not self._times:
            self.video_label.setText("waiting for an image")
            return

        idle = time.time() - self._times[-1]
        if idle < 2.0:
            self.video_label.setText("live")
            return

        self.video_label.setText(f"no image for {idle:.0f}s")
        if idle < self.STALL_RESTART_SECONDS or self._restarting:
            return

        self._restarting = True
        self.statusBar().showMessage("video stalled, restarting capture", 4000)
        old = self.grabber
        try:
            old.frame_ready.disconnect()
            old.status.disconnect()
            old.reconnected.disconnect()
        except (RuntimeError, TypeError):
            pass
        old.stop()
        if old.isRunning():
            # Blocked inside the driver. It cannot be killed safely, and
            # letting it be collected while running aborts Qt, so keep a
            # reference and leave it alone.
            self._abandoned.append(old)
        self._start_grabber()
        self._restarting = False

    def on_reconnected(self) -> None:
        """Rate history from before a dropout would misreport the new stream."""
        self._times.clear()
        self._unique_times.clear()
        self._previous = None

    def on_video_status(self, message: str) -> None:
        self._link_state = message
        self.video_label.setText("live" if message == "connected" else message)
        if message != "connected":
            # Stale readings would otherwise look like live ones.
            self.readout.setText(message)

    def closeEvent(self, event) -> None:
        if self._writer is not None:
            self.on_record(False)
        self.grabber.stop()
        self.control.close()
        super().closeEvent(event)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=int, help="video device index (default: probe)")
    parser.add_argument("--port", help="control port, for example COM55 (default: probe)")
    parser.add_argument("--output", default="captures", help="folder for images and video")
    args = parser.parse_args()

    app = QtWidgets.QApplication(sys.argv)
    window = MainWindow(args)
    window.resize(1100, 660)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
