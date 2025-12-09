"""
Convert a TI-TXT file to framed packets and push to the host MSP430FR5969.

Usage:
   python send*.py
"""
import sys, struct, serial, time
sys.tracebacklimit = 0


START, END, ACK, NACK, FIN = 0x55, 0x5A, 0xAA, 0xEE, 0xAC
FSL = 0xAE
RST = 0x5B # software reset trigger

# Command definitions
CMD_TARGET_SELECT    = 0x11
CMD_TARGET_CHECK     = 0x12
CMD_CHECKSUM_CHECK   = 0x13
CMD_RECEIVE_IMG      = 0x14
CMD_FLASH_IMG        = 0x15
CMD_BAUD_CHANGE      = 0x16
CMD_HOST_BAUD_CHANGE = 0x17
CMD_RESET_TARGET     = 0x19

ERR = {
    0xE1: "size = 0",
    0xE2: "size > 256",
    0xE3: "segment table full",
    0xE4: "image buffer overflow",
    0xE5: "CRC mismatch",
    0xE6: "protocol framing",
    0xE7: "target flash failed",
    0xE8: "BSL version mismatch",
    0xE9: "Target's image is not sent to the Host",#Slave didnt get the correct flash"
    0xEA: "Slave is not available",
    0xEE: "generic NACK",
}

#  map target baud_rate to hex code
baud_map = {
    9600:   0x60,
    19200:  0x61,
    38400:  0x62,
    57600:  0x63,
    115200: 0x64,
    230400: 0x65,
    460800: 0x66,
    921600: 0x67
}

TARGET_1 = 0b00000001
TARGET_2 = 0b00000010
TARGET_3 = 0b00000100
TARGET_4 = 0b00001000

BOARD_1 = 0b00010000
BOARD_2 = 0b00100000
BOARD_3 = 0b01000000

def parse_titxt(fname):
    segs, addr, buf = [], None, []
    with open(fname) as f:
        for ln in f:
            ln = ln.strip()
            if not ln:          continue
            if ln.startswith('@'):
                if addr is not None:
                    segs.append((addr, bytes(buf)))
                addr = int(ln[1:], 16)
                buf  = []
            elif ln[0] in 'qQ':
                if addr is not None: segs.append((addr, bytes(buf)))
                break
            else:
                buf.extend(int(b,16) for b in ln.split())
    
    fixed = []
    total_bytes = 0
    for addr, data in segs: # split into 256-byte chunks
        while data:
            chunk_size = min(len(data), 256)
            chunk = data[:chunk_size]
            fixed.append((addr, chunk))
            total_bytes += len(chunk)
            addr  += len(chunk)
            data   = data[chunk_size:]
            print(len(chunk))

    print(f"{len(segs)} segments, total bytes: {total_bytes}")
    return fixed

def crc16_ccitt(data, seed=0xFFFF):
    crc = seed
    for b in data:                # no byte-reversal here
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def send_seg(seg, ser):
    addr, data = seg
    hdr  = struct.pack('<I', addr) + struct.pack('<H', len(data))
    crc  = crc16_ccitt(hdr + data).to_bytes(2, 'little')
    pkt  = bytes([START]) + hdr + data + crc
    print("Packet header:", [f"{b:02X}" for b in pkt[:6]])
    ser.write(pkt);  ser.flush()

    resp = ser.read(1)
    if not resp:
        print("Timeout/unknown sending pkts");         return False
    code = resp[0]

    if code == ACK:
        return True                      # success
    elif code in ERR:
        print(f"Error {hex(code)}: {ERR[code]}; resending")
        return False                     # will retry
    else:
        print(f"Unexpected byte {hex(code)}");  return False

def send_command(ser, cmd):
    'Send a command byte and wait for acknowledgement of the same command. Retry up to 5 times on timeout.'
    max_retries = 5
    retries = 0
    while retries < max_retries:
        ser.write(bytes([cmd])); ser.flush()
        resp = ser.read(1)
        if not resp:
            retries += 1
            print(f"Timeout waiting for ACK of command {hex(cmd)} (retry {retries}/{max_retries})")
            time.sleep(0.1)
            continue
        code = resp[0]
        if code == cmd:
            return True
        else:
            print(f"Wrong ACK {hex(code)} for command {hex(cmd)}")
            return False
    raise ValueError(f"Failed to get ACK for command {hex(cmd)} after {max_retries} retries.")

def select_target(ser, address):
    '''Send target address and wait for ACK from Host'''
    
    send_command(ser, CMD_TARGET_SELECT)

    if not (0 <= address <= 0xFF):
        raise ValueError(f"Invalid target address {hex(address)}")
    ser.write(bytes([address])); ser.flush()
    resp = ser.read(1)
    if not resp:
        print("Timeout waiting for host to receive target addresses")
        return False
    code = resp[0]
    if code == ACK:
        return True
    elif code in ERR:
        raise ValueError(f"Error {hex(code)}: {ERR[code]}; failed to select target")
    else:
        raise ValueError(f"Unexpected byte {hex(code)} received from host for selecting target")

def check_target(ser, target_id):
    '''Check if target is connected and responding, and print time taken'''
    send_command(ser, CMD_TARGET_CHECK)
    start_time = time.time()

    selected_targets = []
    while True:
        resp = ser.read(1)
        if not resp:
            print("Timeout waiting for target check response...")
            continue
        code = resp[0]
        if code == FIN:
            print("Connected targets:", selected_targets)
            break
        else:
            target_code = code
            ack = ser.read(1)
            board_number = {0b001: 1, 0b010: 2, 0b100: 3}.get((target_code >> 4), 0)
            target_number = {0b0001: 1, 0b0010: 2, 0b0100: 3, 0b1000: 4}.get(target_code & 0x0F, 0)
            if not ack:
                print("Timeout waiting for HOST ACK/NACK while selecting target")
                return False
            ack_code = ack[0]
            if ack_code == ACK:
                selected_targets.append((board_number, target_number))
                elapsed_target = time.time() - start_time
                print(f"Responded: (board {board_number}, target {target_number}) in {elapsed_target:.3f} seconds")
            elif ack_code == NACK:
                print(f"Slave {target_number} from Board {board_number} did not respond ❗")
            elif ack_code == 0x00:
                print(f"Slave {target_number} from Board {board_number} is not connected ❗")
            else:
                print(f"BSL version mismatch byte {hex(ack_code)} for Board {board_number} target {target_number}")
                break
    expected_targets = bin(target_id & 0xF).count('1') * bin((target_id >> 4) & 0xF).count('1')
    if len(selected_targets) != expected_targets:
        raise ValueError("Not all selected targets are connected and responding")
    elapsed = time.time() - start_time
    print(f"Target check time: {elapsed:.2f} seconds")

def send_image(ser, segs):
    '''Send firmware image segments to the host.
    (Should be used after sending RECEIVE_IMAGE cmd)
    '''
    # Send target code to the host
    for s in segs:
        while not send_seg(s, ser):
            pass
    ser.write(bytes([END]))
    if ser.read(1)==bytes([FIN]):
        print("Upload to host complete")
        print("Target(s) reset by Master (1 sec delay)...")
        # continue to monitor
        # sleep for 1 sec
        time.sleep(1)
        return True
    else:
        print("No final ACK on image transfer")
        return False
    
def connect_host(port_path, port, host_baud):
    try:
        ser = serial.Serial(port_path + port, host_baud, timeout=2)
    except serial.SerialException as e:
        print(f"Host is not connected at {port_path + port}: {e}")
        sys.exit(1)
    print(f"Connected to host {ser.name} at {host_baud} baud")
    time.sleep(0.1)
    return ser

def baud_change(ser, target_baud, target_id):
    '''
    Send BAUD_CHANGE command, then desired baud_code.
    Receive confirmation from targets whose baud has changed.
    Check if all selected target_ids are received.
    (Should be used after sending TARGET_SELECT cmd)
    '''
    print(f"Selected Target baud rate = {target_baud}")
    start_time = time.time()
    send_command(ser, CMD_BAUD_CHANGE)

    if target_baud not in baud_map:
        raise ValueError(f"Unsupported baud rate {target_baud}")
    baud_code = baud_map[target_baud]
    ser.write(bytes([baud_code])); ser.flush()

    changed_targets = []
    while True:
        resp = ser.read(1)
        if not resp:
            print("Timeout waiting for baud change response")
            continue
        code = resp[0]
        if code == FIN:
            print("Targets with baud changed:", changed_targets)
            break
        else:
            target_code = code
            ack = ser.read(1)
            board_number = {0b001: 1, 0b010: 2, 0b100: 3}.get((target_code >> 4), 0)
            target_number = {0b0001: 1, 0b0010: 2, 0b0100: 3, 0b1000: 4}.get(target_code & 0x0F, 0)
            if not ack:
                print("Timeout waiting for HOST ACK/NACK while changing baud")
                return False
            ack_code = ack[0]
            if ack_code == ACK:
                elapsed_target = time.time() - start_time
                print(f"Responded: (board {board_number}, target {target_number}) in {elapsed_target:.3f} seconds")
                changed_targets.append((board_number, target_number))
            elif ack_code == NACK:
                print(f"Slave {target_number} from Board {board_number} did not change baud ❗")
            elif ack_code == 0x00:
                print(f"Slave {target_number} from Board {board_number} is not connected ❗")
            else:
                if board_number == 0:
                    print(f"Received {hex(target_number)}")
                else:
                    print(f"BSL version mismatch byte {hex(ack_code)} for Board {board_number} target {target_number}")
                break
    expected_targets = bin(target_id & 0xF).count('1') * bin((target_id >> 4) & 0xF).count('1')
    if len(changed_targets) != expected_targets:
        raise ValueError("Not all selected targets changed baud and responded")
    elapsed = time.time() - start_time
    print(f"Baud change time: {elapsed:.2f} seconds")
    return True

def flash_image(target_id, target_baud, port, fn):
    '''
    Send firmware image to a target device 
    (Select_target + Receive + Flash).
    '''

    segs = parse_titxt(fn)

    # with serial.Serial(port_path + port, baud, timeout=2) as ser:
    ser = connect_host(port_path, port, host_baud)
    
    selected_boards = [str(i - 3) for i in range(4, 7) if (target_id >> i) & 1]
    selected_targets = [str(i + 1) for i in range(4) if (target_id >> i) & 1]
    pairs = []
    for b in selected_boards:
        for s in selected_targets:
            pairs.append(f"(board {b}, target {s})")
    print(f"Selected targets: {', '.join(pairs)}")


    # Reset host node
    ser.write(bytes([RST])); ser.flush()
    time.sleep(0.1)

    # reset the target(s)
    send_command(ser, CMD_RESET_TARGET)
    time.sleep(0.75) #0.5 works

    # select the target(s)
    select_target(ser, target_id)

    # set baud rate of the target(s)
    baud_change(ser, target_baud, target_id)


    
    # check the target(s) connection
    # check_target(ser, target_id)


    # Send receive_image command and wait for ACK
    send_command(ser, CMD_RECEIVE_IMG)

    start_time = time.time()
    if not send_image(ser, segs):
        return

    # Send flash image command and wait for flashing ACK
    send_command(ser, CMD_FLASH_IMG)
    transfer_time = time.time()

    while True:
        resp = ser.read(1)
        if not resp:
            print("Waiting for flashing confirmation...")
            continue
        code = resp[0] 
        if code == FSL:
            print("Flashing complete ✅")
        elif code in ERR:
            print(f"Flash Error {hex(code)}: {ERR[code]}")
        else:
            print(f"Unexpected Flash response ❌: {hex(code)}, {code:08b}, {code}")
        break
    end_time = time.time()
    elapsed = end_time - start_time
    print(f"Total transfer time: {transfer_time - start_time:.2f} seconds")
    print(f"Total flashing time: {end_time - transfer_time:.2f} seconds")
    print(f"Total time taken: {elapsed:.2f} seconds")

def test(target_id, target_baud, port, fn):

    segs = parse_titxt(fn)
    start_time = time.time()

    # with serial.Serial(port_path + port, baud, timeout=2) as ser:
    ser = connect_host(port_path, port, host_baud)
    connect_host_time = time.time() - start_time
    
    selected_boards = [str(i - 3) for i in range(4, 7) if (target_id >> i) & 1]
    selected_targets = [str(i + 1) for i in range(4) if (target_id >> i) & 1]
    pairs = []
    for b in selected_boards:
        for s in selected_targets:
            pairs.append(f"(board {b}, target {s})")
    print(f"Selected targets: {', '.join(pairs)}")


    start_time = time.time()
    # Reset host node
    ser.write(bytes([RST])); ser.flush()
    time.sleep(0.1)
    reset_host_time = time.time() - start_time

    start_time = time.time()
    # reset the target(s)
    send_command(ser, CMD_RESET_TARGET)
    time.sleep(0.75) #0.5 works
    reset_target_time = time.time() - start_time 

    start_time = time.time()
    # select the target(s)
    select_target(ser, target_id)
    select_target_time = time.time() - start_time

    start_time = time.time()
    # set baud rate of the target(s)
    baud_change(ser, target_baud, target_id)
    baud_change_time = time.time() - start_time

    # check the target(s) connection
    # check_target(ser, target_id)

    start_time = time.time()
    # Send receive_image command and wait for ACK
    send_command(ser, CMD_RECEIVE_IMG)

    start_time = time.time()
    if not send_image(ser, segs):
        return
    send_image_time = time.time() - start_time

    # Send flash image command and wait for flashing ACK
    send_command(ser, CMD_FLASH_IMG)
    start_time = time.time()

    while True:
        resp = ser.read(1)
        if not resp:
            print("Waiting for flashing confirmation...")
            continue
        code = resp[0] 
        if code == FSL:
            print("Flashing complete ✅")
        elif code in ERR:
            print(f"Flash Error {hex(code)}: {ERR[code]}")
        else:
            print(f"Unexpected Flash response ❌: {hex(code)}, {code:08b}, {code}")
        break
    flash_image_time = time.time() - start_time
    print(f"Timings:")
    print(f"  connect_host_time: {connect_host_time:.3f} seconds")
    print(f"  reset_host_time: {reset_host_time:.3f} seconds")
    print(f"  reset_target_time: {reset_target_time:.3f} seconds")
    print(f"  select_target_time: {select_target_time:.3f} seconds")
    print(f"  baud_change_time: {baud_change_time:.3f} seconds")
    print(f"  send_image_time: {send_image_time:.3f} seconds")
    print(f"  flash_image_time: {flash_image_time:.3f} seconds")
    total_time = connect_host_time + reset_host_time + reset_target_time + select_target_time + baud_change_time + send_image_time + flash_image_time
    print(f"Total time taken: {total_time:.3f} seconds")



if __name__=="__main__":

    host_baud = 19200 # initial baud to connect to host (fixed)
    port_path = '/dev/tty'  # linux
    # port_path = 'COM'  # windows
    # port_path = '/dev/cu.usbmodem'  # macOS

    # BOARD3:   5959
    # BOARD1,2: 5949
    # B1 -> white headers, B2-> green
    # target_select =   TARGET_1 | BOARD_1 #| BOARD_2
    # target_select = TARGET_1 | TARGET_2 | TARGET_3 | TARGET_4 | BOARD_1 #| BOARD_2
    # target_select = TARGET_1 | TARGET_2 | BOARD_3 #| BOARD_2#| TARGET_2
    target_select = TARGET_1 | TARGET_2 | TARGET_3 | TARGET_4 | BOARD_3 | BOARD_1 | BOARD_2
    # target_baud = 921600
    # target_baud = 460800
    # target_baud = 230400
    # target_baud = 115200
    target_baud = 57600
    # target_baud = 38400
    # target_baud = 19200
    # target_baud = 9600
    # flash_image(target_select, target_baud, 'ACM1', 'App1_5949.txt')
    test(target_select, target_baud, 'ACM1', 'App1_5949.txt')
    sys.exit(0)

        """
        Returns a target_select value for a given device_count (1-12).
        Devices are distributed across boards and targets.
        Some counts (5, 7, 10, 11) are not possible with this mapping.
        """
        # Board and target bitmasks
        boards = [BOARD_1, BOARD_2, BOARD_3]
        targets = [TARGET_1, TARGET_2, TARGET_3, TARGET_4]

        # Precomputed valid combinations for 1-12 devices
        valid_map = {
            1:  boards[0] | targets[0],
            2:  boards[0] | targets[0] | targets[1],
            3:  boards[0] | targets[0] | targets[1] | targets[2],
            4:  boards[0] | targets[0] | targets[1] | targets[2] | targets[3],
            6:  boards[0] | boards[1] | targets[0] | targets[1] | targets[2],
            8:  boards[0] | boards[1] | targets[0] | targets[1] | targets[2] | targets[3],
            9:  boards[0] | boards[1] | boards[2] | targets[0] | targets[1] | targets[2],
            12: boards[0] | boards[1] | boards[2] | targets[0] | targets[1] | targets[2] | targets[3],
        }
        if device_count in valid_map:
            return valid_map[device_count]
        else:
            raise ValueError(f"device_count={device_count} cannot be mapped to a valid target_select (invalid: 5, 7, 10, 11)")