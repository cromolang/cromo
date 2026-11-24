# The values of Cromo in gdb: a string as its text, a list as its elements, a map as
# its keys and values, in place of the address, the length and the capacity that they
# are made of. The extension for Visual Studio Code loads it when a program is
# debugged on Linux; by hand, in gdb: source cromo_gdb.py
import gdb

# The types of C with the size and the sign of those of Cromo, for a program that has a
# list of them but no variable of that type, so that its debug information lacks it.
BASIC = {
    'i8': 'signed char', 'i16': 'short', 'i32': 'int', 'i64': 'long long',
    'u8': 'unsigned char', 'u16': 'unsigned short', 'u32': 'unsigned int', 'u64': 'unsigned long long',
    'f32': 'float', 'f64': 'double', 'bool': 'bool', 'char': 'unsigned char',
}


def type_arguments(name):
    """The type arguments of `map<string, list<i32>>`: ['string', 'list<i32>']."""
    start = name.find('<')
    if start < 0 or not name.endswith('>'):
        return []
    arguments, depth, begin = [], 0, start + 1
    for i in range(start + 1, len(name) - 1):
        c = name[i]
        if c in '<[(':
            depth += 1
        elif c in '>])':
            depth -= 1
        elif c == ',' and depth == 0:
            arguments.append(name[begin:i].strip())
            begin = i + 1
    arguments.append(name[begin:len(name) - 1].strip())
    return arguments


def find_type(name):
    """The type with that name of Cromo, or None when the program does not describe it."""
    try:
        return gdb.lookup_type(name)
    except gdb.error:
        pass
    if name in BASIC:
        try:
            return gdb.lookup_type(BASIC[name])
        except gdb.error:
            pass
    return None


def text_of(value):
    """The text of a string of Cromo: `length` bytes of UTF-8 at `data`."""
    length = int(value['length'])
    if length <= 0:
        return ''
    shown = min(length, 4096)
    data = gdb.selected_inferior().read_memory(int(value['data']), shown)
    text = bytes(data).decode('utf-8', 'replace')
    return text if shown == length else text + '...'


class StringPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return text_of(self.value)

    def display_hint(self):
        return 'string'


class ListPrinter:
    def __init__(self, value, element):
        self.value = value
        self.element = element

    def to_string(self):
        return '%s of %d' % (self.value.type.strip_typedefs().name, int(self.value['length']))

    def children(self):
        if self.element is None:
            return
        data = self.value['data'].cast(self.element.pointer())
        for i in range(min(int(self.value['length']), 10000)):
            yield '[%d]' % i, (data + i).dereference()

    def display_hint(self):
        return 'array'


class MapPrinter:
    def __init__(self, value, key, item):
        self.value = value
        self.key = key
        self.item = item

    def to_string(self):
        return '%s of %d' % (self.value.type.strip_typedefs().name, int(self.value['length']))

    def children(self):
        if self.key is None or self.item is None:
            return
        # A slot: its hash in 8 bytes, the key, and the value at a multiple of 8 (D197).
        value_offset = 8 + (self.key.sizeof + 7) // 8 * 8
        slot_size = value_offset + (self.item.sizeof + 7) // 8 * 8
        slots = int(self.value['slots'])
        word = gdb.lookup_type('unsigned long long').pointer()
        count = 0
        for i in range(int(self.value['capacity'])):
            at = slots + i * slot_size
            if int(gdb.Value(at).cast(word).dereference()) < 0x8000000000000000:
                continue  # empty or removed
            yield 'key%d' % count, gdb.Value(at + 8).cast(self.key.pointer()).dereference()
            yield 'value%d' % count, gdb.Value(at + value_offset).cast(self.item.pointer()).dereference()
            count += 1
            if count >= 10000:
                break

    def display_hint(self):
        return 'map'


def lookup(value):
    type = value.type.strip_typedefs()
    if type.code != gdb.TYPE_CODE_STRUCT or type.name is None:
        return None
    name = type.name
    if name == 'string':
        return StringPrinter(value)
    arguments = type_arguments(name)
    if name.startswith('list<') and len(arguments) == 1:
        return ListPrinter(value, find_type(arguments[0]))
    if name.startswith('map<') and len(arguments) == 2:
        return MapPrinter(value, find_type(arguments[0]), find_type(arguments[1]))
    return None


gdb.pretty_printers.append(lookup)
