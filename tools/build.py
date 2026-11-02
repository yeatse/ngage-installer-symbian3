"""Build native ARM components and compose the unsigned Symbian^3 installer."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from adapt_client import adapt_client, target_component
from component_identity import patch_image, replace_image
from extract_client import extract_client
from merge_packages import merge_embedded
from installserver_compat import adapt_server
from native_package import single_file
from sis import Package

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def compile_native(sdk, name, diagnostic=False):
    project = ROOT / 'native' / name
    build = ROOT / 'build' / (name + ('-diagnostic' if diagnostic else ''))
    (project / 'sdk-location.json').write_text(json.dumps(
        {'sdk': str(sdk), 'python': sys.executable}))
    run(sdk / 'bin/cmake', '-S', project, '-B', build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        f'-DNGAGE_DIAGNOSTIC={"ON" if diagnostic else "OFF"}',
        f'-DCMAKE_TOOLCHAIN_FILE={sdk}/cmake/symbian-arm.cmake',
        f'-DSYMBIAN_SDK_PREFIX={sdk}', f'-DCMAKE_MAKE_PROGRAM={sdk}/bin/ninja')
    run(sdk / 'bin/cmake', '--build', build, '--parallel', '2')
    return build


def component(sdk, name, executable, uid, diagnostic=False, **identity):
    project = ROOT / 'native' / name
    build = compile_native(sdk, name, diagnostic)
    artifact = build / 'e32' / (executable + '.exe')
    output = ROOT / 'build' / (name + ('-diagnostic' if diagnostic else '') + '-package')
    run(Path(sys.executable).parent / 'symbian', 'package', '--project', project,
        '--artifact', artifact, '--output', output)
    packages = list(output.glob('*.sis'))
    if len(packages) != 1:
        raise ValueError('Expected exactly one SDK component package')
    package = Package.parse(packages[0].read_bytes())
    raw = artifact.read_bytes()
    return replace_image(package, raw, patch_image(raw, uid, **identity))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--install-service', type=Path,
                        default=ROOT / 'downloads/ynstallserver-belle-fixture.exe')
    parser.add_argument('--diagnostic', action='store_true', help='Flush phone-side logs to E:/logs/ngage, falling back to C:')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts')
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    source = args.installer.read_bytes()
    manifest = json.loads((ROOT / 'validation/inputs.json').read_text())
    expected = next(item['sha256'] for item in manifest if item['filename'] == 'n97-ngage.sisx')
    if hashlib.sha256(source).hexdigest() != expected:
        raise ValueError('Installer SHA-256 does not match pinned upstream input')
    client = extract_client(Package.parse(source))
    adapted, report = adapt_client(client)
    template = target_component(component(sdk, 'touch', 'ngage_touch', 0xe0000960,
                                          diagnostic=args.diagnostic, capabilities=6144))
    touch_path = ROOT / 'build' / ('touch-diagnostic' if args.diagnostic else 'touch') / 'e32/ngage_touch.exe'
    touch_image = patch_image(touch_path.read_bytes(), 0xe0000960, capabilities=6144)
    touch = single_file(template, touch_image, package_uid=0xe0000961,
                        name='N-Gage background touch service',
                        target=r'C:\sys\bin\ngage_touch.exe', run_after_install=True)
    server_image = adapt_server(args.install_service.read_bytes())
    server = single_file(touch, server_image, package_uid=0xe0000973,
                         name='N-Gage Belle SWI compatibility',
                         target=r'C:\sys\bin\YnstallServer.exe')
    posting_path = compile_native(sdk, 'posting', args.diagnostic) / 'PostingSurfaceFactory.dll'
    # Window/bitmap DLLs provide ALL-TCB; imports must cover their importer's caps.
    posting_image = patch_image(posting_path.read_bytes(), 0xe0000962,
                               uid2=0x1000008d, expected_uid1=0x10000079,
                               expected_uid2=0x1000008d, capabilities=0xffffe)
    posting = single_file(touch, posting_image, package_uid=0xe0000963,
                          name='N-Gage native display compatibility',
                          target=r'C:\sys\bin\PostingSurfaceFactory.dll')
    # Ordinary hacked phones already supply InstallServer. The X7 CFW backend
    # is a separate package variant; replacing a running SWI service is unsafe.
    result = merge_embedded(Package.parse(adapted), [posting, touch]).encode()
    args.output.mkdir(parents=True, exist_ok=True)
    suffix = '-diagnostic' if args.diagnostic else ''
    target = args.output / f'ngage-installer-symbian3{suffix}.sis'
    target.write_bytes(result)
    posting_fingerprint = 2166136261
    for value in posting_image:
        posting_fingerprint = ((posting_fingerprint ^ value) * 16777619) & 0xffffffff
    report.update({'posting_image_fnv1a': f'{posting_fingerprint:08x}',
                   'posting_image_sha256': hashlib.sha256(posting_image).hexdigest(),
                   'sha256': hashlib.sha256(result).hexdigest(), 'bytes': len(result),
                   'signed': False, 'sdk_version': '0.3.0',
                   'original_payload_preserved': True,
                   'guest_install_service': 'existing phone installation service',
                   'guest_display': 'native window and bitmap APIs',
                   'touch_capabilities': ['AllFiles', 'SwEvent'],
                   'posting_capabilities_mask': '000ffffe',
                   'diagnostic': args.diagnostic, 'touch_launcher_icon': False})
    (args.output / f'build-report{suffix}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Built {target}: {report}')
    x7 = merge_embedded(Package.parse(adapted), [server, posting, touch]).encode()
    x7_target = args.output / f'ngage-installer-x7-custom-rom{suffix}.sis'
    x7_target.write_bytes(x7)
    x7_report = dict(report, sha256=hashlib.sha256(x7).hexdigest(), bytes=len(x7),
                     guest_install_service='X7 custom ROM YnstallServer replacement')
    (args.output / f'build-report-x7-custom-rom{suffix}.json').write_text(json.dumps(x7_report, indent=2) + '\n')


if __name__ == '__main__':
    main()
