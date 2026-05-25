use std::env;
use std::error::Error;
use std::fs;
use std::process;

type AnyResult<T> = Result<T, Box<dyn Error>>;

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        process::exit(1);
    }
}

fn run() -> AnyResult<()> {
    let args = env::args().skip(1).collect::<Vec<_>>();
    match args.as_slice() {
        [command, path] if command == "validate-vector-file" => validate_vector_file(path),
        _ => Err("usage: cbor-tags-cbor-diag-interop validate-vector-file <vectors.tsv>".into()),
    }
}

fn validate_vector_file(path: &str) -> AnyResult<()> {
    let contents = fs::read_to_string(path)?;
    let mut valid_count = 0usize;
    let mut malformed_count = 0usize;

    for (line_index, line) in contents.lines().enumerate() {
        let line_number = line_index + 1;
        if line.trim().is_empty() || line.starts_with('#') {
            continue;
        }

        let fields = line.splitn(3, '\t').collect::<Vec<_>>();
        if fields.len() != 3 {
            return Err(
                format!("{path}:{line_number}: expected name, hex, and diagnostic fields").into(),
            );
        }

        let name = fields[0];
        let hex = fields[1];
        let expected_diag = fields[2];
        let bytes =
            decode_hex(hex).map_err(|error| format!("{path}:{line_number}:{name}: {error}"))?;

        if expected_diag == "ERROR" {
            assert_parse_fails(path, line_number, name, hex, &bytes)?;
            malformed_count += 1;
            continue;
        }

        let parsed_from_hex = cbor_diag::parse_hex(hex)
            .map_err(|error| format!("{path}:{line_number}:{name}: parse_hex failed: {error:?}"))?;
        let parsed_from_bytes = cbor_diag::parse_bytes(&bytes).map_err(|error| {
            format!("{path}:{line_number}:{name}: parse_bytes failed: {error:?}")
        })?;

        if parsed_from_hex != parsed_from_bytes {
            return Err(format!("{path}:{line_number}:{name}: parse_hex and parse_bytes produced different data items").into());
        }

        let encoded_bytes = parsed_from_hex.to_bytes();
        if encoded_bytes != bytes {
            return Err(format!(
                "{path}:{line_number}:{name}: cbor-diag byte round-trip changed bytes: expected {}, got {}",
                hex,
                encode_hex(&encoded_bytes)
            )
            .into());
        }

        let actual_diag = parsed_from_hex.to_diag();
        if expected_diag != "-" && actual_diag != expected_diag {
            return Err(format!(
                "{path}:{line_number}:{name}: diagnostic mismatch: expected {expected_diag:?}, got {actual_diag:?}"
            )
            .into());
        }

        let reparsed_diag = cbor_diag::parse_diag(&actual_diag).map_err(|error| {
            format!("{path}:{line_number}:{name}: parse_diag failed: {error:?}")
        })?;
        let reparsed_bytes = reparsed_diag.to_bytes();
        if reparsed_bytes != bytes {
            return Err(format!(
                "{path}:{line_number}:{name}: diagnostic round-trip changed bytes: expected {}, got {}",
                hex,
                encode_hex(&reparsed_bytes)
            )
            .into());
        }

        let normalized_hex = compact_hex(&parsed_from_hex.to_hex());
        if normalized_hex != hex.to_ascii_lowercase() {
            return Err(format!(
                "{path}:{line_number}:{name}: to_hex round-trip changed bytes: expected {}, got {}",
                hex, normalized_hex
            )
            .into());
        }

        valid_count += 1;
    }

    println!("validated {valid_count} cbor_tags vectors and {malformed_count} malformed edge cases through cbor-diag");
    Ok(())
}

fn assert_parse_fails(
    path: &str,
    line_number: usize,
    name: &str,
    hex: &str,
    bytes: &[u8],
) -> AnyResult<()> {
    if let Ok(item) = cbor_diag::parse_hex(hex) {
        return Err(format!("{path}:{line_number}:{name}: parse_hex unexpectedly accepted malformed input as {item:?}").into());
    }

    if let Ok(item) = cbor_diag::parse_bytes(bytes) {
        return Err(format!("{path}:{line_number}:{name}: parse_bytes unexpectedly accepted malformed input as {item:?}").into());
    }

    Ok(())
}

fn decode_hex(hex: &str) -> Result<Vec<u8>, String> {
    let mut compact = String::with_capacity(hex.len());
    for character in hex.chars() {
        if !character.is_ascii_whitespace() {
            compact.push(character);
        }
    }

    if compact.len() % 2 != 0 {
        return Err("hex input has odd length".to_owned());
    }

    let mut bytes = Vec::with_capacity(compact.len() / 2);
    for index in (0..compact.len()).step_by(2) {
        let byte = u8::from_str_radix(&compact[index..index + 2], 16)
            .map_err(|_| format!("invalid hex byte {:?}", &compact[index..index + 2]))?;
        bytes.push(byte);
    }

    Ok(bytes)
}

fn encode_hex(bytes: &[u8]) -> String {
    let mut output = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn compact_hex(text: &str) -> String {
    let mut output = String::new();

    for line in text.lines() {
        let data = line.split_once('#').map_or(line, |(prefix, _)| prefix);
        for character in data.chars() {
            if character.is_ascii_hexdigit() {
                output.push(character.to_ascii_lowercase());
            }
        }
    }

    output
}
