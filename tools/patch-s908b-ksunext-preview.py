#!/usr/bin/env python3

import re
import sys
from pathlib import Path

PROFILE_ID = "b0s-S908BXXSMGZB2-pr272-ksunext-preview"
KSU_NEXT_URL = (
    "https://github.com/sarabpal-dev/KernelSU-Next/releases/download/"
    "v3.3.0-android12-5.10/kernelsu-android12-5.10.ko"
)
KSU_NEXT_SIZE = 461880
KSU_NEXT_SHA256 = "51ba8ddbb1237c44b591c9766d2caf9a221dc45a1c59e9162337c3ff27150e29"
KSU_NEXT_MANAGER_URL = (
    "https://github.com/sarabpal-dev/KernelSU-Next/releases/download/"
    "v3.3.0-android12-5.10/KernelSU_Next_v3.3.0-release.apk"
)
KSU_NEXT_MANAGER_PACKAGE = "com.rifsxd.ksunext"
KSU_NEXT_HOME_URL = "https://github.com/sarabpal-dev/KernelSU-Next"


def patch_payload_repository(path: Path, payload_sha: str) -> None:
    source = path.read_text()

    start = source.index("    fun loadTargets(): List<TargetProfile> {")
    end = source.index("\n\n    fun resolveTarget", start)
    replacement = f'''    fun loadTargets(): List<TargetProfile> = listOf(
        TargetProfile(
            profileId = "{PROFILE_ID}",
            displayName = "Galaxy S22 Ultra SM-S908B | Kernel 5.10.237 | KernelSU Next EXPERIMENTAL",
            models = setOf("SM-S908B"),
            kernelVersions = setOf("5.10.237"),
            exploit = RemoteArtifact(
                url = "https://raw.githubusercontent.com/peppicry/Root-My-Galaxy-Payloads/{payload_sha}/artifacts/b0s-S908BXXSMGZB2/cve-2026-43499-app.so",
                size = 1757752L,
            ),
            kernelSu = RemoteArtifact(
                url = "{KSU_NEXT_URL}",
                size = {KSU_NEXT_SIZE}L,
            ),
        ),
    )'''
    source = source[:start] + replacement + source[end:]

    if "import java.security.MessageDigest" not in source:
        anchor = "import java.net.URL\n"
        if anchor not in source:
            raise RuntimeError("PayloadRepository import anchor not found")
        source = source.replace(anchor, anchor + "import java.security.MessageDigest\n", 1)

    chmod_anchor = "        Os.chmod(kernelSu.absolutePath, 0b100100100)\n"
    if chmod_anchor not in source:
        raise RuntimeError("KernelSU chmod anchor not found")
    source = source.replace(
        chmod_anchor,
        chmod_anchor
        + f'''        if (profile.profileId == "{PROFILE_ID}") {{
            require(sha256(kernelSu) == "{KSU_NEXT_SHA256}") {{
                "KernelSU Next module SHA-256 mismatch"
            }}
        }}
''',
        1,
    )

    helper_anchor = "    private fun resolveMainCommit(): String {\n"
    if helper_anchor not in source:
        raise RuntimeError("PayloadRepository helper anchor not found")
    sha_helper = '''    private fun sha256(file: File): String =
        MessageDigest.getInstance("SHA-256").let { digest ->
            file.inputStream().use { input ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    digest.update(buffer, 0, count)
                }
            }
            digest.digest().joinToString("") {
                "%02x".format(it.toInt() and 0xff)
            }
        }

'''
    source = source.replace(helper_anchor, sha_helper + helper_anchor, 1)
    path.write_text(source)


def patch_install_view_model(path: Path) -> None:
    source = path.read_text()
    start = source.index("    private suspend fun installKernelSu(payloads: VerifiedPayloads) {")
    end = source.index("\n\n    private fun detectInstalled()", start)

    replacement = '''    private suspend fun installKernelSu(payloads: VerifiedPayloads) {
        val modulePath = "/data/local/tmp/kernelsu-next-android12-5.10.ko"

        if (shizukuEnabled()) {
            shizukuStage(payloads.kernelSu, modulePath, "644")
        } else {
            val source = shellQuote(payloads.kernelSu.absolutePath)
            val target = shellQuote(modulePath)
            val stageCommand =
                "/system/bin/cp $source $target && /system/bin/chmod 644 $target"
            val stage = runHelper("-c", stageCommand)
            require(stage.code == 0) {
                app.getString(R.string.error_ksu_stage, stage.output)
            }
        }
        appendLog("[*] KernelSU Next module staged")

        // Do not use /sys/module or /proc/modules as the post-load success gate here.
        // On this Samsung jailbreak path the module can be fully usable by the
        // KernelSU Next Manager while those interfaces are not visible from the
        // Root My Galaxy app/helper path after the LKM changes the security context.
        // A successful insmod return means the kernel accepted the module and its
        // init routine returned success. Functional runtime confirmation is then
        // performed by the matching KernelSU Next Manager / su control channel.
        val load = runHelper(
            "-c",
            "/system/bin/insmod ${shellQuote(modulePath)}",
        )
        if (load.output.isNotBlank()) appendLog(load.output)
        require(load.code == 0) {
            "KernelSU Next insmod failed (${load.code}): ${load.output}"
        }

        storeInstallReceipt()
        appendLog("[+] KernelSU Next LKM load accepted by kernel (insmod rc=0)")
        appendLog("[*] Open the bundled KernelSU Next Manager to confirm runtime root")
    }'''

    path.write_text(source[:start] + replacement + source[end:])


def patch_manager_target(app: Path) -> None:
    main_activity = app / "app/src/main/java/dev/busung/s25uroot/MainActivity.kt"
    source = main_activity.read_text()

    old = '''private const val KERNEL_SU_MANAGER_URL =
    "https://github.com/tiann/KernelSU/releases/download/v3.2.5/KernelSU_v3.2.5_32525-release.apk"
private const val KERNEL_SU_MANAGER_PACKAGE = "me.weishu.kernelsu"
private const val KERNEL_SU_HOME_URL = "https://kernelsu.org/"'''
    new = f'''private const val KERNEL_SU_MANAGER_URL =
    "{KSU_NEXT_MANAGER_URL}"
private const val KERNEL_SU_MANAGER_PACKAGE = "{KSU_NEXT_MANAGER_PACKAGE}"
private const val KERNEL_SU_HOME_URL = "{KSU_NEXT_HOME_URL}"'''

    if old not in source:
        raise RuntimeError("KernelSU Manager constants anchor not found")
    main_activity.write_text(source.replace(old, new, 1))

    manifest = app / "app/src/main/AndroidManifest.xml"
    manifest_source = manifest.read_text()
    old_query = '<package android:name="me.weishu.kernelsu" />'
    new_query = f'<package android:name="{KSU_NEXT_MANAGER_PACKAGE}" />'
    if old_query not in manifest_source:
        raise RuntimeError("KernelSU Manager manifest query anchor not found")
    manifest.write_text(manifest_source.replace(old_query, new_query, 1))


def patch_preview_strings(app: Path) -> None:
    replacements = {
        "values/strings.xml": {
            "step_ksu_title": "Load KernelSU Next",
            "step_ksu_detail": "Load the LKM with bootstrap root",
            "phase_loading_ksu": "Loading KernelSU Next with bootstrap root",
            "phase_installed": "KernelSU Next LKM loaded; open Manager to confirm runtime",
            "status_ksu_loading": "Loading KernelSU Next LKM",
            "status_ksu_active": "KernelSU Next active",
            "install_tap_manager": "Tap to install KernelSU Next Manager",
            "install_tap_open_manager": "Tap to open KernelSU Next Manager",
        },
        "values-pt-rBR/strings.xml": {
            "step_ksu_title": "Carregar KernelSU Next",
            "step_ksu_detail": "Carregar o módulo LKM com o root bootstrap",
            "phase_loading_ksu": "Carregando o KernelSU Next com o root bootstrap",
            "phase_installed": "KernelSU Next carregado; abra o Manager para confirmar o ambiente",
            "status_ksu_loading": "Carregando o módulo KernelSU Next",
            "status_ksu_active": "KernelSU Next ativo",
            "install_tap_manager": "Toque para instalar o Gerenciador do KernelSU Next",
            "install_tap_open_manager": "Toque para abrir o Gerenciador do KernelSU Next",
        },
    }

    for relative, mapping in replacements.items():
        path = app / "app/src/main/res" / relative
        if not path.exists():
            continue
        source = path.read_text()
        for name, value in mapping.items():
            pattern = rf'(<string name="{re.escape(name)}">).*?(</string>)'
            source, count = re.subn(
                pattern,
                lambda match, value=value: match.group(1) + value + match.group(2),
                source,
                count=1,
            )
            if count == 0:
                closing = "</resources>"
                if closing not in source:
                    raise RuntimeError(f"resources closing tag not found in {relative}")
                source = source.replace(
                    closing,
                    f'    <string name="{name}">{value}</string>\n{closing}',
                    1,
                )
        path.write_text(source)


def patch_app_identity(app: Path) -> None:
    gradle = app / "app/build.gradle.kts"
    source = gradle.read_text()
    old = 'applicationId = "dev.busung.s25uroot"'
    new = 'applicationId = "dev.busung.s25uroot.s908bksunextpreview"'
    if old not in source:
        raise RuntimeError("applicationId anchor not found")
    gradle.write_text(source.replace(old, new, 1))

    changed = 0
    for strings in (app / "app/src/main/res").glob("values*/strings.xml"):
        source = strings.read_text()
        patched, count = re.subn(
            r'(<string name="app_name">).*?(</string>)',
            r'\1Root My Galaxy — S908B KSU Next Preview\2',
            source,
            count=1,
        )
        if count:
            strings.write_text(patched)
            changed += 1

    if changed == 0:
        raise RuntimeError("No app_name resource was patched")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: patch-s908b-ksunext-preview.py <Root-My-Galaxy-dir> <payload-sha>"
        )

    app = Path(sys.argv[1])
    payload_sha = sys.argv[2]
    if not re.fullmatch(r"[0-9a-f]{40}", payload_sha):
        raise SystemExit("payload SHA must be a 40-character lowercase Git SHA")

    patch_payload_repository(
        app / "app/src/main/java/dev/busung/s25uroot/PayloadRepository.kt",
        payload_sha,
    )
    patch_install_view_model(
        app / "app/src/main/java/dev/busung/s25uroot/InstallViewModel.kt"
    )
    patch_manager_target(app)
    patch_preview_strings(app)
    patch_app_identity(app)

    print(f"KernelSU Next preview pinned to payload commit {payload_sha}")
    print(f"KernelSU Next module SHA-256: {KSU_NEXT_SHA256}")
    print(f"KernelSU Next Manager package: {KSU_NEXT_MANAGER_PACKAGE}")


if __name__ == "__main__":
    main()
