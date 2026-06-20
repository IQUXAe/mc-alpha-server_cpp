use std::collections::BTreeMap;
use std::io::{Read, Write};

#[derive(Debug, Clone, PartialEq)]
pub enum NbtTag {
    End,
    Byte(i8),
    Short(i16),
    Int(i32),
    Long(i64),
    Float(f32),
    Double(f64),
    ByteArray(Vec<u8>),
    String(String),
    List(NbtList),
    Compound(NbtCompound),
}

#[derive(Debug, Clone, PartialEq)]
pub struct NbtCompound {
    pub map: BTreeMap<String, NbtTag>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct NbtList {
    pub tag_type: u8,
    pub elements: Vec<NbtTag>,
}

pub fn read_tag_type<R: Read>(reader: &mut R) -> std::io::Result<u8> {
    let mut buf = [0u8; 1];
    reader.read_exact(&mut buf)?;
    Ok(buf[0])
}

pub fn write_tag_type<W: Write>(writer: &mut W, tag_type: u8) -> std::io::Result<()> {
    writer.write_all(&[tag_type])
}

pub fn read_string<R: Read>(reader: &mut R) -> std::io::Result<String> {
    let mut len_buf = [0u8; 2];
    reader.read_exact(&mut len_buf)?;
    let len = u16::from_be_bytes(len_buf) as usize;
    let mut bytes = vec![0u8; len];
    reader.read_exact(&mut bytes)?;
    String::from_utf8(bytes).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

pub fn write_string<W: Write>(writer: &mut W, s: &str) -> std::io::Result<()> {
    let bytes = s.as_bytes();
    let len = bytes.len() as u16;
    writer.write_all(&len.to_be_bytes())?;
    writer.write_all(bytes)
}

pub fn read_payload<R: Read>(reader: &mut R, tag_type: u8) -> std::io::Result<NbtTag> {
    match tag_type {
        0 => Ok(NbtTag::End),
        1 => {
            let mut buf = [0u8; 1];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Byte(buf[0] as i8))
        }
        2 => {
            let mut buf = [0u8; 2];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Short(i16::from_be_bytes(buf)))
        }
        3 => {
            let mut buf = [0u8; 4];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Int(i32::from_be_bytes(buf)))
        }
        4 => {
            let mut buf = [0u8; 8];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Long(i64::from_be_bytes(buf)))
        }
        5 => {
            let mut buf = [0u8; 4];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Float(f32::from_be_bytes(buf)))
        }
        6 => {
            let mut buf = [0u8; 8];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Double(f64::from_be_bytes(buf)))
        }
        7 => {
            let mut len_buf = [0u8; 4];
            reader.read_exact(&mut len_buf)?;
            let len = i32::from_be_bytes(len_buf);
            if len < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "negative byte array length"));
            }
            let mut bytes = vec![0u8; len as usize];
            reader.read_exact(&mut bytes)?;
            Ok(NbtTag::ByteArray(bytes))
        }
        8 => {
            let s = read_string(reader)?;
            Ok(NbtTag::String(s))
        }
        9 => {
            let mut type_buf = [0u8; 1];
            reader.read_exact(&mut type_buf)?;
            let tag_type = type_buf[0];
            let mut len_buf = [0u8; 4];
            reader.read_exact(&mut len_buf)?;
            let len = i32::from_be_bytes(len_buf);
            if len < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "negative list length"));
            }
            let mut elements = Vec::with_capacity(len as usize);
            for _ in 0..len {
                elements.push(read_payload(reader, tag_type)?);
            }
            Ok(NbtTag::List(NbtList { tag_type, elements }))
        }
        10 => {
            let mut map = BTreeMap::new();
            loop {
                let mut type_buf = [0u8; 1];
                reader.read_exact(&mut type_buf)?;
                let tag_type = type_buf[0];
                if tag_type == 0 {
                    break;
                }
                let name = read_string(reader)?;
                let val = read_payload(reader, tag_type)?;
                map.insert(name, val);
            }
            Ok(NbtTag::Compound(NbtCompound { map }))
        }
        _ => Err(std::io::Error::new(std::io::ErrorKind::InvalidData, format!("unknown tag type {}", tag_type))),
    }
}

pub fn write_payload<W: Write>(writer: &mut W, tag: &NbtTag) -> std::io::Result<()> {
    match tag {
        NbtTag::End => Ok(()),
        NbtTag::Byte(val) => writer.write_all(&[*val as u8]),
        NbtTag::Short(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Int(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Long(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Float(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Double(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::ByteArray(bytes) => {
            let len = bytes.len() as i32;
            writer.write_all(&len.to_be_bytes())?;
            writer.write_all(bytes)
        }
        NbtTag::String(s) => write_string(writer, s),
        NbtTag::List(list) => {
            writer.write_all(&[list.tag_type])?;
            let len = list.elements.len() as i32;
            writer.write_all(&len.to_be_bytes())?;
            for elem in &list.elements {
                write_payload(writer, elem)?;
            }
            Ok(())
        }
        NbtTag::Compound(comp) => {
            for (name, tag) in &comp.map {
                let tag_type = match tag {
                    NbtTag::End => 0,
                    NbtTag::Byte(_) => 1,
                    NbtTag::Short(_) => 2,
                    NbtTag::Int(_) => 3,
                    NbtTag::Long(_) => 4,
                    NbtTag::Float(_) => 5,
                    NbtTag::Double(_) => 6,
                    NbtTag::ByteArray(_) => 7,
                    NbtTag::String(_) => 8,
                    NbtTag::List(_) => 9,
                    NbtTag::Compound(_) => 10,
                };
                writer.write_all(&[tag_type])?;
                write_string(writer, name)?;
                write_payload(writer, tag)?;
            }
            writer.write_all(&[0]) // TAG_End
        }
    }
}

pub fn read_root<R: Read>(reader: &mut R) -> std::io::Result<(String, NbtCompound)> {
    let mut type_buf = [0u8; 1];
    reader.read_exact(&mut type_buf)?;
    if type_buf[0] != 10 {
        return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "root must be TAG_Compound"));
    }
    let name = read_string(reader)?;
    let tag = read_payload(reader, 10)?;
    if let NbtTag::Compound(comp) = tag {
        Ok((name, comp))
    } else {
        Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "expected NbtCompound"))
    }
}

pub fn write_root<W: Write>(writer: &mut W, name: &str, comp: &NbtCompound) -> std::io::Result<()> {
    writer.write_all(&[10])?;
    write_string(writer, name)?;
    write_payload(writer, &NbtTag::Compound(comp.clone()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compound_roundtrip() {
        let mut comp = NbtCompound { map: BTreeMap::new() };
        comp.map.insert("byteVal".to_string(), NbtTag::Byte(127));
        comp.map.insert("shortVal".to_string(), NbtTag::Short(-30000));
        comp.map.insert("intVal".to_string(), NbtTag::Int(999999));
        comp.map.insert("strVal".to_string(), NbtTag::String("hello".to_string()));
        comp.map.insert("floatVal".to_string(), NbtTag::Float(1.5));
        comp.map.insert("doubleVal".to_string(), NbtTag::Double(2.5));

        let mut out = Vec::new();
        write_root(&mut out, "Root", &comp).unwrap();

        let mut cursor = std::io::Cursor::new(out);
        let (name, read_back) = read_root(&mut cursor).unwrap();

        assert_eq!(name, "Root");
        assert_eq!(read_back, comp);
    }
}
