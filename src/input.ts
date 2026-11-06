// Keyboard + gamepad input state.
export class Input {
  down = new Set<string>();
  pressed = new Set<string>();
  private padPrev: boolean[] = [];

  constructor() {
    window.addEventListener('keydown', (e) => {
      if ((e.target as HTMLElement)?.tagName === 'SELECT') return;
      if (['Tab', 'Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.code)) e.preventDefault();
      if (!this.down.has(e.code)) this.pressed.add(e.code);
      this.down.add(e.code);
    });
    window.addEventListener('keyup', (e) => this.down.delete(e.code));
    window.addEventListener('blur', () => this.down.clear());
  }

  /** Movement vector in screen space: x = right, y = up. */
  move(): { x: number; y: number } {
    let x = 0;
    let y = 0;
    if (this.down.has('KeyW') || this.down.has('ArrowUp')) y += 1;
    if (this.down.has('KeyS') || this.down.has('ArrowDown')) y -= 1;
    if (this.down.has('KeyD') || this.down.has('ArrowRight')) x += 1;
    if (this.down.has('KeyA') || this.down.has('ArrowLeft')) x -= 1;
    const pad = this.pad();
    if (pad) {
      const ax = pad.axes[0] ?? 0;
      const ay = pad.axes[1] ?? 0;
      if (Math.hypot(ax, ay) > 0.18) {
        x = ax;
        y = -ay;
      }
    }
    const l = Math.hypot(x, y);
    return l > 1 ? { x: x / l, y: y / l } : { x, y };
  }

  sprint() {
    const pad = this.pad();
    return this.down.has('ShiftLeft') || this.down.has('ShiftRight') || !!(pad && (pad.buttons[7]?.value ?? 0) > 0.4);
  }

  private pad(): Gamepad | null {
    const ps = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of ps) if (p) return p;
    return null;
  }

  /** Map gamepad buttons to virtual key presses each frame. */
  pollPad() {
    const pad = this.pad();
    if (!pad) return;
    const map: Record<number, string> = { 0: 'Digit1', 1: 'Digit2', 2: 'Digit3', 3: 'Digit4', 5: 'Digit5', 4: 'Tab', 9: 'Escape', 8: 'KeyC', 12: 'Space', 10: 'Space' };
    pad.buttons.forEach((b, i) => {
      const was = this.padPrev[i] ?? false;
      if (b.pressed && !was && map[i]) this.pressed.add(map[i]);
      this.padPrev[i] = b.pressed;
    });
  }

  take(code: string): boolean {
    if (this.pressed.has(code)) {
      this.pressed.delete(code);
      return true;
    }
    return false;
  }

  endFrame() {
    this.pressed.clear();
  }
}
