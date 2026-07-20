# lora_radio.py

import time
import sys


try:
    from ch347api import SPIDevice, SPIClockFreq
except ImportError:
    print("ERROR: ch347api is not installed.")
    print("Run:")
    print("  py -m pip install ch347api hidapi")
    sys.exit(1)


# ============================================================
# RFM95W / SX1276 registers
# ============================================================

REG_FIFO                 = 0x00
REG_OP_MODE              = 0x01
REG_FRF_MSB              = 0x06
REG_FRF_MID              = 0x07
REG_FRF_LSB              = 0x08
REG_PA_CONFIG            = 0x09
REG_LNA                  = 0x0C
REG_FIFO_ADDR_PTR        = 0x0D
REG_FIFO_TX_BASE_ADDR    = 0x0E
REG_FIFO_RX_BASE_ADDR    = 0x0F
REG_FIFO_RX_CURRENT_ADDR = 0x10
REG_IRQ_FLAGS            = 0x12
REG_RX_NB_BYTES          = 0x13
REG_PKT_SNR_VALUE        = 0x19
REG_PKT_RSSI_VALUE       = 0x1A
REG_MODEM_CONFIG_1       = 0x1D
REG_MODEM_CONFIG_2       = 0x1E
REG_PREAMBLE_MSB         = 0x20
REG_PREAMBLE_LSB         = 0x21
REG_PAYLOAD_LENGTH       = 0x22
REG_MODEM_CONFIG_3       = 0x26
REG_SYNC_WORD            = 0x39
REG_DIO_MAPPING_1        = 0x40
REG_VERSION              = 0x42
REG_PA_DAC               = 0x4D


MODE_LONG_RANGE          = 0x80
MODE_SLEEP               = 0x00
MODE_STDBY               = 0x01
MODE_TX                  = 0x03
MODE_RX_CONTINUOUS       = 0x05


IRQ_RX_DONE              = 0x40
IRQ_PAYLOAD_CRC_ERROR    = 0x20
IRQ_TX_DONE              = 0x08


class RFM95Radio:
    def __init__(
        self,
        frequency_hz=869525000,
        spreading_factor=8,
        sync_word=0x12,
        tx_power_dbm=17,
    ):
        self.spi = None

        self.frequency_hz = frequency_hz
        self.spreading_factor = spreading_factor
        self.sync_word = sync_word
        self.tx_power_dbm = tx_power_dbm

    # ========================================================
    # CH347 / Waveshare SPI backend
    # ========================================================

    def open(self):
        """
        Open the CH347 SPI device.

        These settings must match the settings that previously gave:
            RFM95 version register = 0x12
        """

        self.spi = SPIDevice(
            clock_freq_level=SPIClockFreq.f_468K75,
            is_MSB=True,
            mode=0,                  # Keep mode=0 if your previous working script used mode=0
            write_read_interval=2,
            CS1_high=False,
            CS2_high=False,
            is_16bits=False,
        )

        # Give the CH347/HID interface a moment after opening.
        time.sleep(0.2)

    def spi_xfer(self, tx_bytes, retries=3):
        if self.spi is None:
            raise RuntimeError("SPI device not opened. Call radio.open() first.")

        tx = bytes(tx_bytes)

        last_rx = None

        for attempt in range(1, retries + 1):
            try:
                rx = self.spi.writeRead_CS1(tx)
                rx = list(rx)
                last_rx = rx

                if len(rx) == len(tx):
                    return rx

                print(
                    f"WARNING: incomplete SPI read on attempt {attempt}: "
                    f"got {len(rx)} bytes, expected {len(tx)} bytes"
                )

                time.sleep(0.05)

            except Exception as e:
                print(f"WARNING: SPI transfer failed on attempt {attempt}: {e}")
                time.sleep(0.05)

        raise RuntimeError(
            f"SPI transfer failed after {retries} attempts. "
            f"TX={tx.hex(' ')} last RX={last_rx}"
        )

    # ========================================================
    # Basic register operations
    # ========================================================

    def read_reg(self, addr):
        rx = self.spi_xfer([addr & 0x7F, 0x00])

        if len(rx) < 2:
            raise RuntimeError(
                f"SPI register read failed for address 0x{addr:02X}: "
                f"received only {len(rx)} bytes"
            )

        return rx[1]

    def write_reg(self, addr, value):
        self.spi_xfer([addr | 0x80, value & 0xFF])

    def burst_read(self, addr, length):
        tx = [addr & 0x7F] + [0x00] * length
        rx = self.spi_xfer(tx)
        return rx[1:]

    def burst_write(self, addr, data):
        tx = [addr | 0x80] + list(data)
        self.spi_xfer(tx)

    def set_mode(self, mode):
        self.write_reg(REG_OP_MODE, MODE_LONG_RANGE | mode)

    def read_version(self):
        return self.read_reg(REG_VERSION)

    # ========================================================
    # LoRa configuration
    # ========================================================

    def set_frequency(self, freq_hz):
        """
        FRF = frequency_hz * 2^19 / 32 MHz
        """

        frf = int((freq_hz << 19) // 32000000)

        self.write_reg(REG_FRF_MSB, (frf >> 16) & 0xFF)
        self.write_reg(REG_FRF_MID, (frf >> 8) & 0xFF)
        self.write_reg(REG_FRF_LSB, frf & 0xFF)

    def set_tx_power_dbm(self, dbm):
        """
        PA_BOOST output power in the RFM95W normal +2..+17 dBm range.

        Explicitly restore normal RegPaDac mode so a modem previously used for
        a +20 dBm test cannot retain the high-power setting.
        """

        if dbm < 2:
            dbm = 2

        if dbm > 17:
            dbm = 17

        self.write_reg(REG_PA_DAC, 0x84)
        pa_config = 0x80 | (dbm - 2)
        self.write_reg(REG_PA_CONFIG, pa_config)

    def _sf_to_reg(self):
        sf = self.spreading_factor

        if sf < 6:
            sf = 7

        if sf > 12:
            sf = 12

        return sf << 4

    def configure_common(self):
        version = self.read_version()

        if version != 0x12:
            raise RuntimeError(
                f"RFM95W SPI test failed: version=0x{version:02X}, expected 0x12"
            )

        self.set_mode(MODE_SLEEP)
        time.sleep(0.02)

        self.set_frequency(self.frequency_hz)

        self.write_reg(REG_FIFO_TX_BASE_ADDR, 0x00)
        self.write_reg(REG_FIFO_RX_BASE_ADDR, 0x00)
        self.write_reg(REG_FIFO_ADDR_PTR, 0x00)

        # RegModemConfig1:
        # BW = 125 kHz -> 0x70
        # CR = 4/5     -> 0x02
        # Explicit header mode
        self.write_reg(REG_MODEM_CONFIG_1, 0x70 | 0x02)

        # RegModemConfig2:
        # Spreading factor occupies bits 7..4 (SF8 -> 0x80)
        # CRC enabled -> 0x04
        self.write_reg(REG_MODEM_CONFIG_2, self._sf_to_reg() | 0x04)

        # AGC auto on
        self.write_reg(REG_MODEM_CONFIG_3, 0x04)

        # Preamble length = 8
        self.write_reg(REG_PREAMBLE_MSB, 0x00)
        self.write_reg(REG_PREAMBLE_LSB, 0x08)

        # Private LoRa sync word
        self.write_reg(REG_SYNC_WORD, self.sync_word)

        # Improve LNA gain
        self.write_reg(REG_LNA, self.read_reg(REG_LNA) | 0x03)

        self.set_tx_power_dbm(self.tx_power_dbm)

        # Clear IRQ flags
        self.write_reg(REG_IRQ_FLAGS, 0xFF)

    def start_rx_continuous(self):
        self.set_mode(MODE_STDBY)
        time.sleep(0.01)

        # DIO0 = RxDone
        self.write_reg(REG_DIO_MAPPING_1, 0x00)

        # Clear IRQ flags
        self.write_reg(REG_IRQ_FLAGS, 0xFF)

        # FIFO pointer to RX base
        self.write_reg(REG_FIFO_ADDR_PTR, 0x00)

        self.set_mode(MODE_RX_CONTINUOUS)

    def init_rx(self):
        self.configure_common()
        self.start_rx_continuous()

    # ========================================================
    # RX packet logic
    # ========================================================

    def read_packet_if_available(self):
        irq = self.read_reg(REG_IRQ_FLAGS)

        if (irq & IRQ_RX_DONE) == 0:
            return None

        if irq & IRQ_PAYLOAD_CRC_ERROR:
            self.write_reg(REG_IRQ_FLAGS, 0xFF)
            return {
                "crc_error": True,
                "payload": b"",
                "rssi_dbm": None,
                "snr_db": None,
            }

        packet_len = self.read_reg(REG_RX_NB_BYTES)
        current_addr = self.read_reg(REG_FIFO_RX_CURRENT_ADDR)

        self.write_reg(REG_FIFO_ADDR_PTR, current_addr)

        payload_bytes = self.burst_read(REG_FIFO, packet_len)

        raw_snr = self.read_reg(REG_PKT_SNR_VALUE)
        raw_rssi = self.read_reg(REG_PKT_RSSI_VALUE)

        if raw_snr > 127:
            raw_snr -= 256

        snr_db = raw_snr / 4.0
        rssi_dbm = raw_rssi - 157

        self.write_reg(REG_IRQ_FLAGS, 0xFF)

        return {
            "crc_error": False,
            "payload": bytes(payload_bytes),
            "rssi_dbm": rssi_dbm,
            "snr_db": snr_db,
        }

    # ========================================================
    # TX packet logic, kept for future command/dashboard use
    # ========================================================

    def send_packet(self, payload, timeout_s=5.0):
        if isinstance(payload, str):
            payload = payload.encode("ascii")

        if len(payload) == 0:
            raise ValueError("Payload cannot be empty")

        if len(payload) > 255:
            raise ValueError("Payload too large")

        self.set_mode(MODE_STDBY)
        time.sleep(0.01)

        # DIO0 = TxDone
        self.write_reg(REG_DIO_MAPPING_1, 0x40)

        # Clear IRQ flags
        self.write_reg(REG_IRQ_FLAGS, 0xFF)

        # FIFO pointer to TX base
        self.write_reg(REG_FIFO_ADDR_PTR, 0x00)

        self.burst_write(REG_FIFO, payload)

        self.write_reg(REG_PAYLOAD_LENGTH, len(payload))

        self.set_mode(MODE_TX)

        start = time.time()

        while time.time() - start < timeout_s:
            irq = self.read_reg(REG_IRQ_FLAGS)

            if irq & IRQ_TX_DONE:
                self.write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE)
                self.set_mode(MODE_STDBY)
                return True

            time.sleep(0.01)

        self.set_mode(MODE_STDBY)
        return False