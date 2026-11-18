"""The booth's writer: turns a writer job from the game (system prompt, request, fact sheet) into booth dialogue.

Two backends behind one call, write(job) -> text ('' = nothing airs, the booth's templates cover it):
  claude:<model>  the Claude API (anthropic SDK). Default claude-haiku-5-5, one call per piece (the game's own checks
                  catch wrong numbers, names and teams); add '+check' for a second YES/NO fact-check call. The key comes from the environment (ANTHROPIC_API_KEY, or an
                  `ant auth login` profile). Nothing runs on this PC, so the game keeps its frame rate.
  <ollama model>  a local Ollama (http://127.0.0.1:11434), e.g. qwen2.5:14b

Every piece gets a second, separate fact check against the same fact sheet. A NO vetoes it, and the game's own
checks (numbers, names, teams, staleness) still run on whatever passes.

The harness runs it per job:  python booth_writer.py --model claude:claude-opus-5-5 job.json out.txt
"""
import argparse
import json
import os
import sys
import time

CHECK_SYSTEM = 'You are a strict fact checker for a TV sports broadcast. Answer with one word: YES or NO.'


def check_user(facts, text):
    return (facts + '\n\nBroadcast lines:\n' + text + '\n\nIs every statement in these lines supported by the facts above '
            '(right player, right team, right counts, events that really happened, nothing about seasons or history)? Answer YES or NO.')


class ClaudeWriter:
    def __init__(self, model, check=False):
        import anthropic
        self.anthropic = anthropic
        self.model = model
        # short requests: a stuck call must not hold the booth past the point where the line is stale anyway
        self.client = anthropic.Anthropic(timeout=45.0, max_retries=1)
        self.check = check
        self.name = 'claude ' + model + (' +check' if check else '')
        self.tokens_in = self.tokens_out = 0

    def ready(self):
        # credentials resolve at the first call. A missing key is an error the caller logs, not a hang.
        return bool(os.environ.get('ANTHROPIC_API_KEY') or os.environ.get('ANTHROPIC_AUTH_TOKEN')
                    or os.path.isdir(os.path.expanduser('~/.config/anthropic')))

    def chat(self, system, user, effort, max_tokens):
        kw = dict(model=self.model, max_tokens=max_tokens, system=system,
                  messages=[{'role': 'user', 'content': user}], output_config={'effort': effort})
        if 'haiku' in self.model:
            r = self.client.messages.create(**kw)  # (no server-side refusal fallback for Haiku)
        else:
            # a policy decline re-runs on a fallback model inside the same call
            r = self.client.beta.messages.create(**kw, betas=['server-side-fallback-2026-07-01'], fallbacks='default')
        u = r.usage
        self.tokens_in += u.input_tokens + (u.cache_read_input_tokens or 0) + (u.cache_creation_input_tokens or 0)
        self.tokens_out += u.output_tokens
        if os.environ.get('BOOTH_WRITER_USAGE'):  # (the harness: one line per call, summed into a cost after the game)
            with open(os.environ['BOOTH_WRITER_USAGE'], 'a') as f:
                f.write('%d %d\n' % (u.input_tokens, u.output_tokens))
        if r.stop_reason == 'refusal':
            return ''
        return ''.join(b.text for b in r.content if b.type == 'text').strip()

    def write(self, job):
        # low effort: two or three short spoken lines from a fact sheet (cost was the problem, Fred 2026-11-13)
        text = self.chat(job['system'], job['user'], 'low', 2000)
        if not text:
            return '', 'empty'
        if not self.check:
            return text, 'ok'
        verdict = self.chat(CHECK_SYSTEM, check_user(job.get('facts', ''), text), 'low', 2000)
        if 'YES' not in verdict.upper():
            return '', 'veto: ' + text.replace('\n', ' / ')[:160]
        return text, 'ok'


class OllamaWriter:
    def __init__(self, model, threads=0):
        self.model = model
        self.threads = threads
        self.url = 'http://127.0.0.1:11434'
        self.name = 'ollama %s (%s)' % (model, 'cpu' if threads > 0 else 'gpu')

    def ready(self):
        import urllib.request
        try:
            tags = json.loads(urllib.request.urlopen(self.url + '/api/tags', timeout=3).read())
            return any(m.get('name', '').startswith(self.model) for m in tags.get('models', []))
        except Exception:
            return False

    def chat(self, system, user, predict):
        import urllib.request
        b = json.dumps({'model': self.model, 'stream': False, 'keep_alive': '30m',
                        'options': dict({'temperature': 0.75, 'top_p': 0.9, 'num_predict': predict},
                                        **({'num_gpu': 0, 'num_thread': self.threads} if self.threads > 0 else {})),
                        'messages': [{'role': 'system', 'content': system}, {'role': 'user', 'content': user}]}).encode()
        r = urllib.request.urlopen(urllib.request.Request(self.url + '/api/chat', b, {'Content-Type': 'application/json'}), timeout=180)
        return json.loads(r.read())['message']['content']

    def write(self, job):
        text = self.chat(job['system'], job['user'], 200)
        verdict = self.chat(CHECK_SYSTEM, check_user(job.get('facts', ''), text), 4)
        if 'YES' not in verdict.upper():
            return '', 'veto: ' + text.replace('\n', ' / ')[:160]
        return text, 'ok'


def make(spec, threads=0):
    """'claude:<model>' (or a bare 'claude-...' id) -> Claude API; anything else is an Ollama model name."""
    if spec.startswith('claude'):
        check = spec.endswith('+check')
        model = spec[:-len('+check')] if check else spec
        model = model[len('claude:'):] if model.startswith('claude:') else model
        return ClaudeWriter(model if model.startswith('claude-') else 'claude-haiku-5-5', check)
    return OllamaWriter(spec, threads)


def write_safe(writer, job, log=print):
    """write() that never raises: an API error, a bad key, no internet -> '' (the templates speak instead)."""
    try:
        text, why = writer.write(job)
        if why != 'ok':
            log('writer', why)
        return text
    except Exception as e:
        name = type(e).__name__
        if name == 'AuthenticationError':
            log('writer failed: the Claude API key was refused (set ANTHROPIC_API_KEY)')
        else:
            log('writer failed:', name, str(e)[:200])
        return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True)
    ap.add_argument('job')
    ap.add_argument('out')
    a = ap.parse_args()
    with open(a.job, encoding='utf-8') as f:
        job = json.load(f)
    t0 = time.time()
    text = write_safe(make(a.model), job, log=lambda *x: print(*x, file=sys.stderr))
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write(text)
    print('%.1f' % (time.time() - t0))


if __name__ == '__main__':
    main()
