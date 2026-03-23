#!/usr/bin/env python3
"""
PlatformIO post-build script: Upload firmware binary to AWS S3

Required environment variables:
    S3_BUCKET: S3 bucket name
    S3_PREFIX: S3 key prefix (e.g., 'fu/firmware/esp32s3/')
    AWS_REGION: AWS region (optional, default: ap-northeast-1)

AWS credentials are read from ~/.aws/credentials or IAM role
"""

Import("env")
import os
import sys
from datetime import datetime

# Ensure PlatformIO's Python site-packages is in sys.path for boto3 import
# This fixes VS Code PlatformIO extension issue where boto3 is not found
platformio_python = os.path.expanduser("~/.pyenv/versions/3.8.13/bin/python3.8")
if sys.executable != platformio_python:
    # Add PlatformIO Python's site-packages to sys.path
    import site
    site_packages = os.path.expanduser("~/.pyenv/versions/3.8.13/lib/python3.8/site-packages")
    if site_packages not in sys.path:
        sys.path.insert(0, site_packages)


def upload_to_s3(source, target, env):
    """Upload firmware binary to S3 after successful build"""

    # Get configuration from platformio.ini (priority) or environment variables (fallback)
    s3_bucket = env.GetProjectOption("custom_s3_bucket", os.getenv("S3_BUCKET", ""))
    s3_prefix = env.GetProjectOption("custom_s3_prefix", os.getenv("S3_PREFIX", ""))
    aws_region = env.GetProjectOption("custom_aws_region", os.getenv("AWS_REGION", "ap-northeast-1"))
    custom_name = env.GetProjectOption("custom_firmware_name", "")

    # Validate required configuration
    if not s3_bucket:
        print("INFO: S3 upload disabled.")
        print("      To enable, set 'custom_s3_bucket' in platformio.ini")
        print("      or S3_BUCKET environment variable.")
        print("      See S3_UPLOAD_README.md for details.")
        return

    # Try to import boto3 - if fails, use subprocess with correct Python
    try:
        import boto3
        from botocore.exceptions import ClientError, NoCredentialsError
    except ImportError as e:
        # Fallback: Use subprocess with guaranteed correct Python environment
        print("INFO: Using alternative Python environment for S3 upload...")

        import subprocess
        platformio_python = os.path.expanduser("~/.pyenv/versions/3.8.13/bin/python3.8")

        # Get firmware binary path
        firmware_path = str(target[0])

        # Get project directory from SCons environment (avoid __file__ which may not be defined)
        project_dir = env.get("PROJECT_DIR")
        wrapper_script = os.path.join(project_dir, "scripts", "upload_to_s3_wrapper.py")

        # Call the wrapper script with correct Python
        cmd_args = [
            platformio_python,
            wrapper_script,
            firmware_path,
            s3_bucket,
            s3_prefix,
            aws_region,
            env.get("PIOENV", "unknown"),
            env.get("BOARD", "unknown"),
            project_dir  # Add project_dir for version extraction
        ]
        if custom_name:
            cmd_args.append(custom_name)

        # Call the wrapper script with correct Python
        result = subprocess.run(cmd_args, capture_output=True, text=True)

        # Print output
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)

        if result.returncode != 0:
            print(f"ERROR: S3 upload failed with exit code {result.returncode}")
            sys.exit(1)

        return

    # Get firmware binary path
    firmware_path = str(target[0])
    firmware_name = os.path.basename(firmware_path)

    # Extract firmware version from main.cpp
    firmware_version = "unknown"
    project_dir = env.get("PROJECT_DIR")
    main_cpp_path = os.path.join(project_dir, "src", "main.cpp")

    try:
        with open(main_cpp_path, 'r') as f:
            for line in f:
                if '#define FIRMWARE_VERSION' in line:
                    # Extract version string from: #define FIRMWARE_VERSION "2.0.0"
                    firmware_version = line.split('"')[1]
                    break
    except Exception as e:
        print(f"WARNING: Could not extract firmware version: {e}")

    # Generate timestamp for metadata
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name = os.path.splitext(firmware_name)[0]
    extension = os.path.splitext(firmware_name)[1]

    # Get project name and board from env
    project_name = env.get("PIOENV", "unknown")

    print("=" * 60)
    print("AWS S3 Upload Configuration:")
    print(f"  Bucket:       {s3_bucket}")
    print(f"  Region:       {aws_region}")
    print(f"  Source:       {firmware_path}")
    print(f"  Version:      {firmware_version}")
    print(f"  Project:      {project_name}")
    print("=" * 60)

    try:
        # Create S3 client
        s3_client = boto3.client('s3', region_name=aws_region)

        # Generate distinct filename
        if custom_name:
            base_name = custom_name
        else:
            # Generate distinct filename from prefix (e.g., tube_v2_unit1/ -> tube_v2_unit1)
            prefix_clean = s3_prefix.rstrip('/')
            if prefix_clean:
                base_name = prefix_clean.split('/')[-1]
            else:
                base_name = "firmware"



        # Also upload with version in filename (e.g., firmware_v2.0.0.bin)
        if firmware_version != "unknown":
            version_key = os.path.join(s3_prefix, f"{base_name}_v{firmware_version}{extension}").replace("\\", "/")
            print(f"Uploading versioned file to s3://{s3_bucket}/{version_key}...")

            s3_client.upload_file(
                firmware_path,
                s3_bucket,
                version_key,
                ExtraArgs={
                    'Metadata': {
                        'project': project_name,
                        'firmware-version': firmware_version,
                        'build-timestamp': timestamp,
                        'board': str(env.get("BOARD", "unknown"))
                    }
                }
            )

        # Upload "product" alias (latest stable release link)
        product_key = os.path.join(s3_prefix, f"{base_name}_product{extension}").replace("\\", "/")
        print(f"Uploading product alias to s3://{s3_bucket}/{product_key}...")
        s3_client.upload_file(
            firmware_path,
            s3_bucket,
            product_key,
            ExtraArgs={
                'Metadata': {
                    'project': project_name,
                    'firmware-version': firmware_version,
                    'build-timestamp': timestamp,
                    'board': str(env.get("BOARD", "unknown")),
                    'alias': 'product'
                }
            }
        )

        print("✅ Upload successful!")
        # print(f"   Latest:      s3://{s3_bucket}/{latest_key}")
        if firmware_version != "unknown":
            print(f"   Versioned:   s3://{s3_bucket}/{version_key}")
            print(f"   Product:     s3://{s3_bucket}/{product_key}")
        print("=" * 60)

    except NoCredentialsError:
        print("ERROR: AWS credentials not found.")
        print("       Configure credentials in ~/.aws/credentials or use IAM role")
        sys.exit(1)

    except ClientError as e:
        error_code = e.response.get('Error', {}).get('Code', 'Unknown')
        error_message = e.response.get('Error', {}).get('Message', str(e))
        print(f"ERROR: AWS S3 upload failed ({error_code})")
        print(f"       {error_message}")
        sys.exit(1)

    except Exception as e:
        print(f"ERROR: Unexpected error during S3 upload: {str(e)}")
        sys.exit(1)


# Register post-build action
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", upload_to_s3)
