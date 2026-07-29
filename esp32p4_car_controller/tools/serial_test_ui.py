import queue
import re
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import serial
except Exception as exc:
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None


DEFAULT_PORT = "COM14"
DEFAULT_BAUD = 115200

LINK_RE = re.compile(
    r"link online=(?P<online>\d+) tx=(?P<tx>\d+) tx_fail=(?P<tx_fail>\d+) "
    r"rx_bytes=(?P<rx_bytes>\d+) rx_frames=(?P<rx_frames>\d+) "
    r"checksum_err=(?P<checksum_err>\d+) bad_len=(?P<bad_len>\d+) "
    r"unknown=(?P<unknown>\d+) ack=(?P<ack>\d+) "
    r"last_tx=(?P<last_tx>0x[0-9A-Fa-f]+) last_rx=(?P<last_rx>0x[0-9A-Fa-f]+) "
    r"rx_age_ms=(?P<rx_age_ms>\d+)"
)

STATUS_RE = re.compile(
    r"stm32=(?P<stm32>\d+) flags=(?P<flags>\d+) dist_x10=(?P<dist>\d+) "
    r"temp=(?P<temp>\d+) hum=(?P<hum>\d+) dht=(?P<dht>\d+) "
    r"motor=(?P<motor_state>\d+)/(?P<motor_target>\d+)/(?P<motor_current>\d+) "
    r"gimbal=(?P<pan>\d+)/(?P<tilt>\d+) "
    r"behavior=(?P<behavior>\d+)/(?P<behavior_running>\d+)/(?P<behavior_step>\d+) "
    r"gps=(?P<gps>.*)"
)

NET_RE = re.compile(
    r"network backend=(?P<backend>\S+) started=(?P<started>\d+) "
    r"connected=(?P<connected>\d+) native_wifi=(?P<native_wifi>\d+) "
    r"qwen_state=(?P<qwen_state>\S+) qwen_ready=(?P<qwen_ready>\d+)"
)


class SerialTestUi(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ESP32-P4 小车中文测试面板")
        self.geometry("1120x760")
        self.minsize(980, 650)
        self.option_add("*Font", ("Microsoft YaHei UI", 10))

        self.serial_port = None
        self.reader_thread = None
        self.reader_stop = threading.Event()
        self.rx_queue = queue.Queue()
        self.parse_buffer = ""
        self.auto_poll_enabled = tk.BooleanVar(value=True)
        self.move_warning_seen = False
        self.status_vars = {}

        self._build_ui()
        self.after(80, self._drain_rx_queue)
        self.after(1800, self._auto_poll)

        if serial is None:
            messagebox.showerror(
                "缺少 pyserial",
                "当前 Python 环境不能导入 pyserial，请用 ESP-IDF 的 Python 运行。\n"
                f"错误：{SERIAL_IMPORT_ERROR}",
            )

    def _build_ui(self):
        root = ttk.Frame(self, padding=12)
        root.pack(fill=tk.BOTH, expand=True)

        top = ttk.Frame(root)
        top.pack(fill=tk.X)

        ttk.Label(top, text="串口").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=DEFAULT_PORT)
        ttk.Entry(top, textvariable=self.port_var, width=10).pack(side=tk.LEFT, padx=(6, 14))

        ttk.Label(top, text="波特率").pack(side=tk.LEFT)
        self.baud_var = tk.StringVar(value=str(DEFAULT_BAUD))
        ttk.Entry(top, textvariable=self.baud_var, width=8).pack(side=tk.LEFT, padx=(6, 14))

        self.connect_btn = ttk.Button(top, text="连接", command=self.connect)
        self.connect_btn.pack(side=tk.LEFT)
        self.disconnect_btn = ttk.Button(top, text="断开", command=self.disconnect, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=(8, 0))

        ttk.Checkbutton(top, text="自动刷新 link/status", variable=self.auto_poll_enabled).pack(side=tk.LEFT, padx=16)
        self.connection_var = tk.StringVar(value="未连接")
        ttk.Label(top, textvariable=self.connection_var).pack(side=tk.RIGHT)

        body = ttk.PanedWindow(root, orient=tk.HORIZONTAL)
        body.pack(fill=tk.BOTH, expand=True, pady=(12, 0))

        left = ttk.Frame(body)
        right = ttk.Frame(body)
        body.add(left, weight=1)
        body.add(right, weight=2)

        self._build_status_panel(left)
        self._build_command_panel(left)
        self._build_log_panel(right)

    def _build_status_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="解析后的状态", padding=10)
        frame.pack(fill=tk.X)

        fields = [
            ("stm32", "STM32"),
            ("online", "通信"),
            ("rx", "接收帧"),
            ("errors", "错误"),
            ("ack", "ACK"),
            ("distance", "距离/温湿度"),
            ("motor", "电机"),
            ("gimbal", "云台"),
            ("gps", "GPS"),
            ("net", "网络"),
        ]
        for row, (key, label) in enumerate(fields):
            ttk.Label(frame, text=label, width=12).grid(row=row, column=0, sticky="w", pady=3)
            var = tk.StringVar(value="-")
            self.status_vars[key] = var
            ttk.Label(frame, textvariable=var, wraplength=360).grid(row=row, column=1, sticky="w", pady=3)

    def _build_command_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="测试按钮", padding=10)
        frame.pack(fill=tk.X, pady=(12, 0))

        buttons = [
            ("通信状态", "link", False),
            ("小车状态", "status", False),
            ("网络状态", "net", False),
            ("立即停车", "stop", False),
            ("云台回中", "gimbal 90 90 500", False),
            ("云台 100/100", "gimbal 100 100 500", False),
            ("云台扫描", "gimbal_action scan_slow", False),
            ("灯效 cute", "led cute", False),
            ("灯效 thinking", "led thinking", False),
            ("前进 0.5 秒", "move forward 20 500", True),
            ("后退 0.5 秒", "move backward 20 500", True),
            ("左旋 0.4 秒", "move rotate_left 20 400", True),
            ("拍照触发", "capture", False),
        ]

        for i, (text, command, is_move) in enumerate(buttons):
            btn = ttk.Button(
                frame,
                text=text,
                command=lambda c=command, m=is_move: self.send_command(c, confirm_move=m),
            )
            btn.grid(row=i // 2, column=i % 2, sticky="ew", padx=4, pady=4)
        frame.columnconfigure(0, weight=1)
        frame.columnconfigure(1, weight=1)

        custom = ttk.Frame(frame)
        custom.grid(row=(len(buttons) + 1) // 2, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        self.custom_var = tk.StringVar()
        ttk.Entry(custom, textvariable=self.custom_var).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(custom, text="发送", command=self._send_custom).pack(side=tk.LEFT, padx=(6, 0))

        ttk.Label(
            frame,
            text="测试电机前请把车轮架空，或确保周围没有障碍物。",
            foreground="#9a5a00",
        ).grid(row=(len(buttons) + 3) // 2, column=0, columnspan=2, sticky="w", pady=(8, 0))

    def _build_log_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="串口日志", padding=8)
        frame.pack(fill=tk.BOTH, expand=True)

        toolbar = ttk.Frame(frame)
        toolbar.pack(fill=tk.X, pady=(0, 6))
        ttk.Button(toolbar, text="清空日志", command=self.clear_log).pack(side=tk.LEFT)

        self.log = tk.Text(frame, height=24, wrap=tk.WORD, font=("Consolas", 10))
        scroll = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.log.yview)
        self.log.configure(yscrollcommand=scroll.set)
        self.log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)

    def connect(self):
        if serial is None:
            messagebox.showerror("缺少 pyserial", "当前 Python 环境不能使用 pyserial。")
            return
        if self.serial_port is not None:
            return
        try:
            baud = int(self.baud_var.get().strip())
            port = self.port_var.get().strip()
            self.serial_port = serial.Serial(port, baud, timeout=0.1, write_timeout=1)
            self.serial_port.dtr = False
            self.serial_port.rts = False
        except Exception as exc:
            self.serial_port = None
            messagebox.showerror(
                "连接失败",
                f"无法打开串口。\n\n{exc}\n\n请先关闭 idf.py monitor、VS Code 串口监视器或其他占用 COM14 的串口工具。",
            )
            return

        self.reader_stop.clear()
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_btn.configure(state=tk.DISABLED)
        self.disconnect_btn.configure(state=tk.NORMAL)
        self.connection_var.set(f"已连接 {self.port_var.get().strip()}")
        self._append_log("\n[界面] 已连接，自动发送 link/status...\n")
        self.send_command("link")
        self.send_command("status")

    def disconnect(self):
        self.reader_stop.set()
        port = self.serial_port
        self.serial_port = None
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        self.connect_btn.configure(state=tk.NORMAL)
        self.disconnect_btn.configure(state=tk.DISABLED)
        self.connection_var.set("未连接")
        self._append_log("\n[界面] 已断开。\n")

    def _reader_loop(self):
        while not self.reader_stop.is_set():
            port = self.serial_port
            if port is None:
                break
            try:
                data = port.read(512)
            except Exception as exc:
                self.rx_queue.put(("error", f"\n[界面] 串口读取错误：{exc}\n"))
                break
            if data:
                self.rx_queue.put(("data", data.decode("utf-8", errors="replace")))
            else:
                time.sleep(0.03)

    def _drain_rx_queue(self):
        try:
            while True:
                kind, payload = self.rx_queue.get_nowait()
                self._append_log(payload)
                if kind == "data":
                    self._parse_text(payload)
                elif kind == "error":
                    self.disconnect()
        except queue.Empty:
            pass
        self.after(80, self._drain_rx_queue)

    def _parse_text(self, text):
        self.parse_buffer = (self.parse_buffer + text)[-6000:]

        for match in LINK_RE.finditer(self.parse_buffer):
            d = match.groupdict()
            self.status_vars["online"].set(
                f"online={d['online']}，最后收到={d['last_rx']}，间隔={d['rx_age_ms']} ms"
            )
            self.status_vars["rx"].set(f"字节={d['rx_bytes']}，帧={d['rx_frames']}")
            self.status_vars["errors"].set(
                f"checksum={d['checksum_err']}，bad_len={d['bad_len']}，unknown={d['unknown']}"
            )
            if d["online"] == "1" and d["ack"] == "0" and int(d["tx"]) > 5:
                self.status_vars["ack"].set("0：STM32 未确认控制命令，请重点查 GPIO47 TX -> PA10 RX")
            else:
                self.status_vars["ack"].set(d["ack"])

        for match in STATUS_RE.finditer(self.parse_buffer):
            d = match.groupdict()
            self.status_vars["stm32"].set(f"stm32={d['stm32']}，flags={d['flags']}")
            try:
                dist = int(d["dist"])
                dist_text = "无效" if dist == 65535 else f"{dist / 10:.1f} cm"
            except ValueError:
                dist_text = d["dist"]
            self.status_vars["distance"].set(
                f"{dist_text}，DHT={d['dht']}，温度={d['temp']}，湿度={d['hum']}"
            )
            self.status_vars["motor"].set(
                f"状态={d['motor_state']}，目标={d['motor_target']}，当前={d['motor_current']}"
            )
            self.status_vars["gimbal"].set(f"水平={d['pan']}，俯仰={d['tilt']}")
            self.status_vars["gps"].set(d["gps"][:80] if d["gps"] else "-")

        for match in NET_RE.finditer(self.parse_buffer):
            d = match.groupdict()
            self.status_vars["net"].set(
                f"{d['backend']}，connected={d['connected']}，qwen={d['qwen_state']}，ready={d['qwen_ready']}"
            )

    def send_command(self, command, confirm_move=False):
        command = command.strip()
        if self.serial_port is None:
            messagebox.showwarning("未连接", "请先点击“连接”。")
            return
        if confirm_move and not self.move_warning_seen:
            ok = messagebox.askyesno(
                "电机测试确认",
                "电机命令会让车轮转动。请先把车轮架空，或确保周围安全。\n\n现在发送吗？",
            )
            if not ok:
                return
            self.move_warning_seen = True
        try:
            self.serial_port.write((command + "\n").encode("ascii"))
            self.serial_port.flush()
            self._append_log(f"\n[界面 -> 小车] {command}\n")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))
            self.disconnect()

    def _send_custom(self):
        command = self.custom_var.get().strip()
        if command:
            self.send_command(command)

    def _auto_poll(self):
        if self.auto_poll_enabled.get() and self.serial_port is not None:
            self.send_command("link")
            self.send_command("status")
        self.after(2500, self._auto_poll)

    def _append_log(self, text):
        self.log.insert(tk.END, text)
        self.log.see(tk.END)

    def clear_log(self):
        self.log.delete("1.0", tk.END)

    def destroy(self):
        self.disconnect()
        super().destroy()


if __name__ == "__main__":
    app = SerialTestUi()
    app.mainloop()
