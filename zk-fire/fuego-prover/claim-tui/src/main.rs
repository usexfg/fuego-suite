use anyhow::{bail, Result};
use crossterm::{
    event::{self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyEventKind},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span, Text},
    widgets::{Block, Borders, Clear, InputMode, List, ListItem, ListState, Paragraph, Wrap},
    Frame, Terminal,
};
use serde::{Deserialize, Serialize};
use std::{
    io::{self, Write},
    path::PathBuf,
    process::Command,
    time::Duration,
};
use tiny_keccak::{Hasher, Keccak};

#[derive(Debug, Clone)]
enum Step {
    Welcome,
    EnterTxnHash,
    EnterSecret,
    EnterAmount,
    EnterRecipient,
    EnterRpc,
    Generating,
    Complete,
    Error,
}

#[derive(Debug)]
enum InputTarget {
    TxnHash,
    Secret,
    Amount,
    Recipient,
    Rpc,
}

#[derive(Debug)]
enum InputModeState {
    Normal,
    Editing(InputTarget),
}

struct App {
    step: Step,
    input_mode: InputModeState,
    txn_hash: String,
    secret: String,
    amount: String,
    recipient: String,
    rpc: String,
    status_messages: Vec<String>,
    list_state: ListState,
    running: bool,
    error_message: String,
    bundle_path: Option<PathBuf>,
}

impl Default for App {
    fn default() -> Self {
        let mut list_state = ListState::default();
        list_state.select(Some(0));
        Self {
            step: Step::Welcome,
            input_mode: InputModeState::Normal,
            txn_hash: String::new(),
            secret: String::new(),
            amount: "8000000".to_string(),
            recipient: String::new(),
            rpc: "http://localhost:18180".to_string(),
            status_messages: vec!["Welcome to Fuego HEAT Claim TUI".to_string()],
            list_state,
            running: true,
            error_message: String::new(),
            bundle_path: None,
        }
    }
}

#[derive(Serialize)]
struct ClaimBundle {
    stark_proof: String,
    commitment: String,
    nullifier: String,
    amount: u64,
    txn_hash: String,
    merkle_proof: Vec<String>,
    leaf_index: usize,
    recipient: String,
}

fn main() -> Result<()> {
    setup_terminal()?;
    let backend = CrosstermBackend::new(io::stdout());
    let mut terminal = Terminal::new(backend)?;
    let mut app = App::default();
    let res = run_app(&mut terminal, &mut app);
    restore_terminal()?;
    if let Err(e) = res {
        eprintln!("Error: {}", e);
    }
    Ok(())
}

fn setup_terminal() -> Result<()> {
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen, EnableMouseCapture)?;
    Ok(())
}

fn restore_terminal() -> Result<()> {
    disable_raw_mode()?;
    execute!(
        io::stdout(),
        LeaveAlternateScreen,
        DisableMouseCapture
    )?;
    Ok(())
}

fn run_app<B: ratatui::backend::Backend>(
    terminal: &mut Terminal<B>,
    app: &mut App,
) -> Result<()> {
    loop {
        terminal.draw(|f| ui(f, app))?;
        if let Event::Key(key) = event::read()? {
            if key.kind == KeyEventKind::Press {
                match app.input_mode {
                    InputModeState::Normal => match key.code {
                        KeyCode::Char('q') => app.running = false,
                        KeyCode::Enter => proceed_to_next_step(app),
                        KeyCode::Down => {
                            if let Step::Welcome = app.step {
                                let i = match app.list_state.selected() {
                                    Some(i) => (i + 1) % 5,
                                    None => 0,
                                };
                                app.list_state.select(Some(i));
                            }
                        }
                        KeyCode::Up => {
                            if let Step::Welcome = app.step {
                                let i = match app.list_state.selected() {
                                    Some(i) => (i + 4) % 5,
                                    None => 0,
                                };
                                app.list_state.select(Some(i));
                            }
                        }
                        KeyCode::Char('e') => {
                            app.input_mode = InputModeState::Editing(InputTarget::TxnHash);
                        }
                        KeyCode::Char('s') => {
                            app.input_mode = InputModeState::Editing(InputTarget::Secret);
                        }
                        KeyCode::Char('a') => {
                            app.input_mode = InputModeState::Editing(InputTarget::Amount);
                        }
                        KeyCode::Char('r') => {
                            app.input_mode = InputModeState::Editing(InputTarget::Recipient);
                        }
                        KeyCode::Char('c') => {
                            app.input_mode = InputModeState::Editing(InputTarget::Rpc);
                        }
                        KeyCode::Char('g') => {
                            if validate_inputs(app) {
                                app.step = Step::Generating;
                                if let Err(e) = generate_bundle(app) {
                                    app.error_message = e.to_string();
                                    app.step = Step::Error;
                                } else {
                                    app.step = Step::Complete;
                                }
                            } else {
                                app.status_messages.push("Please fill all fields correctly".to_string());
                            }
                        }
                        _ => {}
                    },
                    InputModeState::Editing(ref mut target) => match key.code {
                        KeyCode::Esc => {
                            app.input_mode = InputModeState::Normal;
                        }
                        KeyCode::Enter => {
                            app.input_mode = InputModeState::Normal;
                        }
                        KeyCode::Char(c) => {
                            match target {
                                InputTarget::TxnHash => app.txn_hash.push(c),
                                InputTarget::Secret => app.secret.push(c),
                                InputTarget::Amount => {
                                    if c.is_numeric() {
                                        app.amount.push(c);
                                    }
                                }
                                InputTarget::Recipient => app.recipient.push(c),
                                InputTarget::Rpc => app.rpc.push(c),
                            }
                        }
                        KeyCode::Backspace => {
                            match target {
                                InputTarget::TxnHash => { app.txn_hash.pop(); }
                                InputTarget::Secret => { app.secret.pop(); }
                                InputTarget::Amount => { app.amount.pop(); }
                                InputTarget::Recipient => { app.recipient.pop(); }
                                InputTarget::Rpc => { app.rpc.pop(); }
                            }
                        }
                        _ => {}
                    },
                }
            }
        }
        if !app.running {
            break;
        }
    }
    Ok(())
}

fn validate_inputs(app: &App) -> bool {
    app.txn_hash.len() == 64
        && app.secret.len() == 64
        && !app.amount.is_empty()
        && app.recipient.starts_with("0x")
        && !app.rpc.is_empty()
}

fn proceed_to_next_step(app: &mut App) {
    match app.step {
        Step::Welcome => {
            app.step = Step::EnterTxnHash;
        }
        Step::EnterTxnHash => {
            if app.txn_hash.len() == 64 {
                app.step = Step::EnterSecret;
            } else {
                app.status_messages.push("Transaction hash must be 64 hex characters".to_string());
            }
        }
        Step::EnterSecret => {
            if app.secret.len() == 64 {
                app.step = Step::EnterAmount;
            } else {
                app.status_messages.push("Secret must be 64 hex characters".to_string());
            }
        }
        Step::EnterAmount => {
            if !app.amount.is_empty() {
                app.step = Step::EnterRecipient;
            } else {
                app.status_messages.push("Amount cannot be empty".to_string());
            }
        }
        Step::EnterRecipient => {
            if app.recipient.starts_with("0x") {
                app.step = Step::EnterRpc;
            } else {
                app.status_messages.push("Recipient must be 0x-prefixed".to_string());
            }
        }
        Step::EnterRpc => {
            if !app.rpc.is_empty() {
                app.step = Step::Generating;
                if let Err(e) = generate_bundle(app) {
                    app.error_message = e.to_string();
                    app.step = Step::Error;
                } else {
                    app.step = Step::Complete;
                }
            }
        }
        Step::Complete | Step::Error => {
            app.step = Step::Welcome;
        }
        _ => {}
    }
}

fn compute_commitment(secret: &str, amount: u64, network_id: u32, chain_id: u32, version: u32, term: u32) -> Result<String> {
    let secret_bytes = hex::decode(secret)?;
    let mut preimage = Vec::new();
    preimage.extend_from_slice(&secret_bytes);
    preimage.extend_from_slice(&amount.to_le_bytes());
    preimage.extend_from_slice(&network_id.to_le_bytes());
    preimage.extend_from_slice(&chain_id.to_le_bytes());
    preimage.extend_from_slice(&version.to_le_bytes());
    preimage.extend_from_slice(&term.to_le_bytes());
    
    let mut hasher = Keccak::v256();
    hasher.update(&preimage);
    let mut output = [0u8; 32];
    hasher.finalize(&mut output);
    
    Ok(format!("0x{}", hex::encode(output)))
}

fn compute_nullifier(secret: &str, amount: u64) -> Result<String> {
    let secret_bytes = hex::decode(secret)?;
    let mut preimage = Vec::new();
    preimage.extend_from_slice(&secret_bytes);
    preimage.extend_from_slice(b"nullifier");
    preimage.extend_from_slice(&amount.to_le_bytes());
    
    let mut hasher = Keccak::v256();
    hasher.update(&preimage);
    let mut output = [0u8; 32];
    hasher.finalize(&mut output);
    
    Ok(format!("0x{}", hex::encode(output)))
}

fn generate_bundle(app: &mut App) -> Result<()> {
    app.status_messages.push("Generating STARK proof...".to_string());
    
    let amount: u64 = app.amount.parse().unwrap_or(8000000);
    
    let package_data = serde_json::json!({
        "metadata": {
            "version": "3.0.0",
            "network": "fuego-mainnet",
            "created_at": chrono::Utc::now().to_rfc3339(),
            "description": "TUI generated"
        },
        "burn_transaction": {
            "transaction_hash": app.txn_hash,
            "burn_amount_xfg": amount as f64 / 10_000_000.0,
            "burn_amount_atomic": amount,
            "block_height": 800001,
            "timestamp": "now",
            "network_id": "1",
            "target_chain_id": 42161,
            "deposit_term": 4294967295
        },
        "recipient": {
            "ethereum_address": app.recipient
        },
        "secret": {
            "secret_key": app.secret
        }
    });
    
    let package_file = "/tmp/fuego_package.json";
    std::fs::write(package_file, serde_json::to_string_pretty(&package_data)?)?;
    
    let proof_file = "/tmp/fuego_proof.json";
    
    let stark_output = Command::new("cargo")
        .args(["run", "-p", "xfg-stark-cli", "--", "generate", package_file, &app.recipient, proof_file])
        .current_dir(std::env::current_dir()?)
        .output()?;
    
    if !stark_output.status.success() {
        bail!("STARK proof generation failed: {}", String::from_utf8_lossy(&stark_output.stderr));
    }
    
    let secret_bytes = hex::decode(&app.secret)?;
    let mut preimage = Vec::new();
    preimage.extend_from_slice(&secret_bytes);
    preimage.extend_from_slice(&amount.to_le_bytes());
    preimage.extend_from_slice(&1u32.to_le_bytes());
    preimage.extend_from_slice(&42161u32.to_le_bytes());
    preimage.extend_from_slice(&3u32.to_le_bytes());
    preimage.extend_from_slice(&0xFFFFFFFFu32.to_le_bytes());
    let preimage_hex = hex::encode(&preimage);
    
    let merkle_file = "/tmp/fuego_merkle.json";
    let merkle_output = Command::new("cargo")
        .args(["run", "-p", "fuego-prover-cli", "--", "claim", "--rpc", &app.rpc, "--commitment", "0x", "--preimage", &preimage_hex, "--recipient", &app.recipient, "--out", merkle_file])
        .current_dir(std::env::current_dir()?)
        .output()?;
    
    if !merkle_output.status.success() {
        bail!("Merkle proof generation failed: {}", String::from_utf8_lossy(&merkle_output.stderr));
    }
    
    let merkle_data: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(merkle_file)?)?;
    let stark_data: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(proof_file)?)?;
    
    let commitment = compute_commitment(&app.secret, amount, 1, 42161, 3, 0xFFFFFFFF)?;
    let nullifier = compute_nullifier(&app.secret, amount)?;
    
    let bundle = ClaimBundle {
        stark_proof: serde_json::to_string(&stark_data["proof_data"]).unwrap_or_default(),
        commitment,
        nullifier,
        amount,
        txn_hash: app.txn_hash.clone(),
        merkle_proof: merkle_data["merkle_proof"]
            .as_array()
            .map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect())
            .unwrap_or_default(),
        leaf_index: merkle_data["leaf_index"].as_u64().unwrap_or(0) as usize,
        recipient: app.recipient.clone(),
    };
    
    let output_path = PathBuf::from("bundle.json");
    std::fs::write(&output_path, serde_json::to_string_pretty(&bundle)?)?;
    
    app.bundle_path = Some(output_path);
    app.status_messages.push("Bundle generated successfully!".to_string());
    
    std::fs::remove_file(package_file).ok();
    std::fs::remove_file(proof_file).ok();
    std::fs::remove_file(merkle_file).ok();
    
    Ok(())
}

fn ui(f: &mut Frame, app: &mut App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .margin(1)
        .constraints([
            Constraint::Length(3),
            Constraint::Min(0),
            Constraint::Length(7),
            Constraint::Length(3),
        ])
        .split(f.area());

    let title = Paragraph::new("🔥 Fuego HEAT Claim TUI")
        .style(Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD))
        .block(Block::default().borders(Borders::ALL));
    f.render_widget(title, chunks[0]);

    match &app.step {
        Step::Welcome => render_welcome(f, app, chunks[1]),
        Step::EnterTxnHash => render_input(f, app, chunks[1], "Transaction Hash (64 hex chars)", &app.txn_hash, InputTarget::TxnHash),
        Step::EnterSecret => render_input(f, app, chunks[1], "Secret (64 hex chars)", &app.secret, InputTarget::Secret),
        Step::EnterAmount => render_input(f, app, chunks[1], "Amount (atomic units, default: 8000000)", &app.amount, InputTarget::Amount),
        Step::EnterRecipient => render_input(f, app, chunks[1], "Recipient (0x-prefixed ETH address)", &app.recipient, InputTarget::Recipient),
        Step::EnterRpc => render_input(f, app, chunks[1], "RPC URL (default: http://localhost:18180)", &app.rpc, InputTarget::Rpc),
        Step::Generating => render_generating(f, app, chunks[1]),
        Step::Complete => render_complete(f, app, chunks[1]),
        Step::Error => render_error(f, app, chunks[1]),
    }

    let status = Paragraph::new(app.status_messages.join("\n"))
        .style(Style::default().fg(Color::Yellow))
        .block(Block::default().borders(Borders::ALL).title("Status"));
    f.render_widget(status, chunks[2]);

    let help = match app.input_mode {
        InputModeState::Normal => Paragraph::new(
            "Keys: [Enter] Proceed | [e] Edit TX | [s] Edit Secret | [a] Edit Amount | [r] Edit Recipient | [c] Edit RPC | [g] Generate | [q] Quit"
        ),
        InputModeState::Editing(_) => Paragraph::new(
            "Keys: [Enter] Confirm | [Esc] Cancel | [Backspace] Delete"
        ),
    }
    .style(Style::default().fg(Color::White))
    .block(Block::default().borders(Borders::ALL).title("Help"));
    f.render_widget(help, chunks[3]);
}

fn render_welcome(f: &mut Frame, app: &mut App, area: Rect) {
    let items: Vec<ListItem> = vec![
        ListItem::new("1. Enter Transaction Hash"),
        ListItem::new("2. Enter Secret"),
        ListItem::new("3. Enter Amount"),
        ListItem::new("4. Enter Recipient Address"),
        ListItem::new("5. Enter RPC URL"),
    ];

    let list = List::new(items)
        .block(Block::default().borders(Borders::ALL).title("Claim Steps"))
        .highlight_style(Style::default().add_modifier(Modifier::REVERSED))
        .highlight_symbol(">> ");

    f.render_stateful_widget(list, area, &mut app.list_state);
}

fn render_input(f: &mut Frame, app: &mut App, area: Rect, title: &str, value: &str, target: InputTarget) {
    let input_style = match app.input_mode {
        InputModeState::Editing(ref t) if *t == target => Style::default().fg(Color::Yellow),
        _ => Style::default().fg(Color::White),
    };

    let input = Paragraph::new(value.as_str())
        .style(input_style)
        .block(Block::default().borders(Borders::ALL).title(title));
    f.render_widget(input, area);

    if let InputModeState::Editing(ref t) = app.input_mode {
        if *t == target {
            f.set_cursor_position((area.x + value.len() as u16 + 1, area.y + 1));
        }
    }
}

fn render_generating(f: &mut Frame, _app: &mut App, area: Rect) {
    let text = Paragraph::new("⚡ Generating proofs...\n\nThis may take a few minutes.")
        .style(Style::default().fg(Color::Cyan))
        .block(Block::default().borders(Borders::ALL).title("Processing"))
        .wrap(Wrap { trim: true });
    f.render_widget(text, area);
}

fn render_complete(f: &mut Frame, app: &mut App, area: Rect) {
    let bundle_info = match &app.bundle_path {
        Some(path) => format!("✅ Bundle generated successfully!\n\nOutput: {}", path.display()),
        None => "✅ Complete!".to_string(),
    };

    let text = Paragraph::new(bundle_info)
        .style(Style::default().fg(Color::Green))
        .block(Block::default().borders(Borders::ALL).title("Success"))
        .wrap(Wrap { trim: true });
    f.render_widget(text, area);
}

fn render_error(f: &mut Frame, app: &mut App, area: Rect) {
    let text = Paragraph::new(format!("❌ Error:\n\n{}", app.error_message))
        .style(Style::default().fg(Color::Red))
        .block(Block::default().borders(Borders::ALL).title("Error"))
        .wrap(Wrap { trim: true });
    f.render_widget(text, area);
}
