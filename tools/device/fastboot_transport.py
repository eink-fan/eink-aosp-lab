"""Standard fastboot over PyUSB/libusb; no model or operation policy here."""
import ctypes.util


class Fastboot:
    def __init__(self, serial, library=None):
        import usb.core
        import usb.util
        import usb.backend.libusb1
        self.util = usb.util
        backend = usb.backend.libusb1.get_backend(
            find_library=lambda name: library or ctypes.util.find_library(name))
        if backend is None:
            raise RuntimeError('libusb unavailable; supply the host library path')
        matches = []
        for device in usb.core.find(find_all=True, backend=backend):
            try:
                if usb.util.get_string(device, device.iSerialNumber) != serial:
                    continue
                for interface in device.get_active_configuration():
                    if (interface.bInterfaceClass, interface.bInterfaceSubClass,
                            interface.bInterfaceProtocol) == (255, 66, 3):
                        matches.append((device, interface))
            except usb.core.USBError:
                continue
        if len(matches) != 1:
            raise RuntimeError('require one serial-bound fastboot interface')
        self.device, interface = matches[0]
        self.number = interface.bInterfaceNumber
        endpoints = [e for e in interface if usb.util.endpoint_type(e.bmAttributes) == 2]
        incoming = [e for e in endpoints if usb.util.endpoint_direction(e.bEndpointAddress) == 128]
        outgoing = [e for e in endpoints if usb.util.endpoint_direction(e.bEndpointAddress) == 0]
        if len(incoming) != 1 or len(outgoing) != 1:
            raise RuntimeError('require one bulk IN/OUT pair')
        self.incoming, self.outgoing = incoming[0], outgoing[0]
        usb.util.claim_interface(self.device, self.number)

    def close(self):
        self.util.dispose_resources(self.device)

    def response(self):
        for _ in range(100):
            data = bytes(self.incoming.read(4096, timeout=30000))
            if data.startswith(b'INFO'):
                continue
            if data[:4] not in (b'OKAY', b'DATA'):
                raise RuntimeError('fastboot failed or returned unknown status; do not replay')
            return data
        raise RuntimeError('no terminal fastboot response')

    def write(self, data):
        if self.outgoing.write(data, timeout=30000) != len(data):
            raise RuntimeError('short USB write; operation state is uncertain')

    def command(self, command):
        self.write(command.encode('ascii'))
        return self.response()

    def getvar(self, name):
        reply = self.command('getvar:' + name)
        if not reply.startswith(b'OKAY'):
            raise RuntimeError('unexpected getvar response')
        return reply[4:].decode('ascii')

    def okay(self, command):
        if not self.command(command).startswith(b'OKAY'):
            raise RuntimeError('expected terminal OKAY')

    def download(self, data):
        if self.command(f'download:{len(data):08x}') != f'DATA{len(data):08x}'.encode():
            raise RuntimeError('download size mismatch')
        for pos in range(0, len(data), 1024 * 1024):
            self.write(data[pos:pos + 1024 * 1024])
        if not self.response().startswith(b'OKAY'):
            raise RuntimeError('download not accepted')
