# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any

from oa_cli import __version__
from oa_cli.classic_integration import ClassicIntegrationError
from oa_cli.classic_integration import validate_classic_integration
from oa_cli.model import ModelError
from oa_cli.model import load_model
from oa_cli.model import validation_summary
from oa_cli.model import write_generated_model
from oa_cli.delivery import DeliveryExportError
from oa_cli.delivery import write_supplier_delivery_bundle
from oa_cli.inspection import diagnostics_status
from oa_cli.inspection import debug_bundle
from oa_cli.inspection import function_group_list
from oa_cli.inspection import health_show
from oa_cli.inspection import machine_show
from oa_cli.inspection import make_context
from oa_cli.inspection import package_list
from oa_cli.inspection import platform_status
from oa_cli.inspection import process_inspect
from oa_cli.inspection import process_list
from oa_cli.inspection import service_list
from oa_cli.inspection import service_watch
from oa_cli.inspection import state_show
from oa_cli.inspection import trace_export
from oa_cli.inspection import update_status
from oa_cli.oem_export import EvidenceExportError
from oa_cli.oem_export import write_evidence_bundle


def _json_dump(data: dict[str, Any], output: Path | None) -> None:
    payload = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if output is None:
        sys.stdout.write(payload)
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(payload, encoding="utf-8")


def _git_commit(repo: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return "unknown"

    return result.stdout.strip()


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def command_host_info(args: argparse.Namespace) -> int:
    data = {
        "tool_version": __version__,
        "python": sys.version.split()[0],
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
    }
    _json_dump(data, args.output)
    return 0


def command_validate_model(args: argparse.Namespace) -> int:
    try:
        data = validation_summary(load_model(args.model))
    except ModelError as exc:
        raise SystemExit(str(exc)) from exc
    _json_dump(data, args.output)
    return 0


def command_generate(args: argparse.Namespace) -> int:
    try:
        write_generated_model(load_model(args.model), args.output.resolve(), __version__)
    except ModelError as exc:
        raise SystemExit(str(exc)) from exc
    return 0


def command_provenance(args: argparse.Namespace) -> int:
    repo = _repo_root()
    data = {
        "autosar_profile": "adaptive-r22-11",
        "build_profile": args.profile,
        "commit": _git_commit(repo),
        "target": args.target,
        "tool_version": __version__,
    }
    _json_dump(data, args.output)
    return 0


def command_export_evidence(args: argparse.Namespace) -> int:
    try:
        write_evidence_bundle(
            repo_root=args.repo_root,
            package_root=args.package_root,
            output=args.output,
            model=args.model,
            generated=args.generated,
            evidence_dir=args.evidence_dir,
            test_results_dir=args.test_results_dir,
            profile=args.profile,
            target=args.target,
            tool_version=__version__,
            strict=args.strict,
        )
    except EvidenceExportError as exc:
        raise SystemExit(str(exc)) from exc
    return 0


def command_export_delivery(args: argparse.Namespace) -> int:
    try:
        write_supplier_delivery_bundle(
            repo_root=args.repo_root,
            package_root=args.package_root,
            output=args.output,
            model=args.model,
            generated=args.generated,
            evidence_dir=args.evidence_dir,
            test_results_dir=args.test_results_dir,
            oem_export=args.oem_export,
            profile=args.profile,
            target=args.target,
            tool_version=__version__,
        )
    except DeliveryExportError as exc:
        raise SystemExit(str(exc)) from exc
    return 0


def command_validate_classic_integration(args: argparse.Namespace) -> int:
    try:
        data = validate_classic_integration(
            repo_root=args.repo_root,
            config=args.config,
            generated=args.generated,
            evidence_dir=args.evidence_dir,
            tool_version=__version__,
        )
    except ClassicIntegrationError as exc:
        raise SystemExit(str(exc)) from exc
    _json_dump(data, args.output)
    return 0


def _inspection_context(args: argparse.Namespace):
    return make_context(
        repo_root=args.repo_root,
        generated=args.generated,
        evidence_dir=args.evidence_dir,
        test_results_dir=args.test_results_dir,
        package_root=args.package_root,
    )


def command_status(args: argparse.Namespace) -> int:
    _json_dump(platform_status(_inspection_context(args)), args.output)
    return 0


def command_machine_show(args: argparse.Namespace) -> int:
    _json_dump(machine_show(_inspection_context(args)), args.output)
    return 0


def command_process_list(args: argparse.Namespace) -> int:
    _json_dump(process_list(_inspection_context(args)), args.output)
    return 0


def command_process_inspect(args: argparse.Namespace) -> int:
    _json_dump(process_inspect(_inspection_context(args), args.name), args.output)
    return 0


def command_function_group_list(args: argparse.Namespace) -> int:
    _json_dump(function_group_list(_inspection_context(args)), args.output)
    return 0


def command_state_show(args: argparse.Namespace) -> int:
    _json_dump(state_show(_inspection_context(args)), args.output)
    return 0


def command_service_list(args: argparse.Namespace) -> int:
    _json_dump(service_list(_inspection_context(args)), args.output)
    return 0


def command_service_watch(args: argparse.Namespace) -> int:
    _json_dump(service_watch(_inspection_context(args)), args.output)
    return 0


def command_health_show(args: argparse.Namespace) -> int:
    _json_dump(health_show(_inspection_context(args)), args.output)
    return 0


def command_package_list(args: argparse.Namespace) -> int:
    _json_dump(package_list(_inspection_context(args)), args.output)
    return 0


def command_update_status(args: argparse.Namespace) -> int:
    _json_dump(update_status(_inspection_context(args)), args.output)
    return 0


def command_diagnostics_status(args: argparse.Namespace) -> int:
    _json_dump(diagnostics_status(_inspection_context(args)), args.output)
    return 0


def command_trace_export(args: argparse.Namespace) -> int:
    _json_dump(trace_export(_inspection_context(args)), args.output)
    return 0


def command_debug_bundle(args: argparse.Namespace) -> int:
    _json_dump(debug_bundle(_inspection_context(args)), args.output)
    return 0


def _add_inspection_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo-root", type=Path, default=_repo_root())
    parser.add_argument("--generated", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--test-results-dir", type=Path)
    parser.add_argument("--package-root", type=Path)
    parser.add_argument("--output", type=Path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="oa")
    subcommands = parser.add_subparsers(dest="command", required=True)

    status = subcommands.add_parser("status")
    _add_inspection_args(status)
    status.set_defaults(func=command_status)

    machine = subcommands.add_parser("machine")
    machine_subcommands = machine.add_subparsers(dest="machine_command", required=True)
    machine_show_parser = machine_subcommands.add_parser("show")
    _add_inspection_args(machine_show_parser)
    machine_show_parser.set_defaults(func=command_machine_show)

    process = subcommands.add_parser("process")
    process_subcommands = process.add_subparsers(dest="process_command", required=True)
    process_list_parser = process_subcommands.add_parser("list")
    _add_inspection_args(process_list_parser)
    process_list_parser.set_defaults(func=command_process_list)
    process_inspect_parser = process_subcommands.add_parser("inspect")
    process_inspect_parser.add_argument("name")
    _add_inspection_args(process_inspect_parser)
    process_inspect_parser.set_defaults(func=command_process_inspect)

    function_group = subcommands.add_parser("function-group")
    function_group_subcommands = function_group.add_subparsers(
        dest="function_group_command",
        required=True,
    )
    function_group_list_parser = function_group_subcommands.add_parser("list")
    _add_inspection_args(function_group_list_parser)
    function_group_list_parser.set_defaults(func=command_function_group_list)

    state = subcommands.add_parser("state")
    state_subcommands = state.add_subparsers(dest="state_command", required=True)
    state_show_parser = state_subcommands.add_parser("show")
    _add_inspection_args(state_show_parser)
    state_show_parser.set_defaults(func=command_state_show)

    service = subcommands.add_parser("service")
    service_subcommands = service.add_subparsers(dest="service_command", required=True)
    service_list_parser = service_subcommands.add_parser("list")
    _add_inspection_args(service_list_parser)
    service_list_parser.set_defaults(func=command_service_list)
    service_watch_parser = service_subcommands.add_parser("watch")
    _add_inspection_args(service_watch_parser)
    service_watch_parser.set_defaults(func=command_service_watch)

    health = subcommands.add_parser("health")
    health_subcommands = health.add_subparsers(dest="health_command", required=True)
    health_show_parser = health_subcommands.add_parser("show")
    _add_inspection_args(health_show_parser)
    health_show_parser.set_defaults(func=command_health_show)

    package = subcommands.add_parser("package")
    package_subcommands = package.add_subparsers(dest="package_command", required=True)
    package_list_parser = package_subcommands.add_parser("list")
    _add_inspection_args(package_list_parser)
    package_list_parser.set_defaults(func=command_package_list)

    update = subcommands.add_parser("update")
    update_subcommands = update.add_subparsers(dest="update_command", required=True)
    update_status_parser = update_subcommands.add_parser("status")
    _add_inspection_args(update_status_parser)
    update_status_parser.set_defaults(func=command_update_status)

    diagnostics = subcommands.add_parser("diagnostics")
    diagnostics_subcommands = diagnostics.add_subparsers(
        dest="diagnostics_command",
        required=True,
    )
    diagnostics_status_parser = diagnostics_subcommands.add_parser("status")
    _add_inspection_args(diagnostics_status_parser)
    diagnostics_status_parser.set_defaults(func=command_diagnostics_status)

    trace = subcommands.add_parser("trace")
    trace_subcommands = trace.add_subparsers(dest="trace_command", required=True)
    trace_export_parser = trace_subcommands.add_parser("export")
    _add_inspection_args(trace_export_parser)
    trace_export_parser.set_defaults(func=command_trace_export)

    debug = subcommands.add_parser("debug")
    debug_subcommands = debug.add_subparsers(dest="debug_command", required=True)
    debug_bundle_parser = debug_subcommands.add_parser("bundle")
    _add_inspection_args(debug_bundle_parser)
    debug_bundle_parser.set_defaults(func=command_debug_bundle)

    host_info = subcommands.add_parser("host-info")
    host_info.add_argument("--output", type=Path)
    host_info.set_defaults(func=command_host_info)

    validate = subcommands.add_parser("validate-model")
    validate.add_argument("--model", type=Path, required=True)
    validate.add_argument("--output", type=Path)
    validate.set_defaults(func=command_validate_model)

    generate = subcommands.add_parser("generate")
    generate.add_argument("--model", type=Path, required=True)
    generate.add_argument("--output", type=Path, required=True)
    generate.set_defaults(func=command_generate)

    provenance = subcommands.add_parser("provenance")
    provenance.add_argument("--target", default="qemu-x86_64")
    provenance.add_argument("--profile", default="dev")
    provenance.add_argument("--output", type=Path)
    provenance.set_defaults(func=command_provenance)

    export = subcommands.add_parser("export-evidence")
    export.add_argument("--repo-root", type=Path, default=_repo_root())
    export.add_argument("--package-root", type=Path, required=True)
    export.add_argument("--output", type=Path, required=True)
    export.add_argument("--model", type=Path, required=True)
    export.add_argument("--generated", type=Path, required=True)
    export.add_argument("--evidence-dir", type=Path, required=True)
    export.add_argument("--test-results-dir", type=Path, required=True)
    export.add_argument("--profile", default="generic-oem")
    export.add_argument("--target", default="qemu-x86_64")
    export.add_argument("--strict", action="store_true")
    export.set_defaults(func=command_export_evidence)

    delivery = subcommands.add_parser("export-delivery")
    delivery.add_argument("--repo-root", type=Path, default=_repo_root())
    delivery.add_argument("--package-root", type=Path, required=True)
    delivery.add_argument("--output", type=Path, required=True)
    delivery.add_argument("--model", type=Path, required=True)
    delivery.add_argument("--generated", type=Path, required=True)
    delivery.add_argument("--evidence-dir", type=Path, required=True)
    delivery.add_argument("--test-results-dir", type=Path, required=True)
    delivery.add_argument("--oem-export", type=Path)
    delivery.add_argument("--profile", default="generic-supplier")
    delivery.add_argument("--target", default="qemu-x86_64")
    delivery.set_defaults(func=command_export_delivery)

    classic = subcommands.add_parser("validate-classic-integration")
    classic.add_argument("--repo-root", type=Path, default=_repo_root())
    classic.add_argument(
        "--config",
        type=Path,
        default=_repo_root() / "integration/virtual-vehicle/classic-gateway/gateway-config.json",
    )
    classic.add_argument(
        "--generated",
        type=Path,
        default=_repo_root() / "out/generated/classic-integration",
    )
    classic.add_argument(
        "--evidence-dir",
        type=Path,
        default=_repo_root() / "out/evidence/classic-integration",
    )
    classic.add_argument("--output", type=Path)
    classic.set_defaults(func=command_validate_classic_integration)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
