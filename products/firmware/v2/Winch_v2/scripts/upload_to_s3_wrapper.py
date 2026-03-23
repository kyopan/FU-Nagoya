#!/usr/bin/env python3
"""
Wrapper script for S3 upload via subprocess
This ensures boto3 is imported in the correct Python environment
"""

import sys
import os
from datetime import datetime

def main():
    if len(sys.argv) < 8:
        print("Usage: upload_to_s3_wrapper.py <firmware_path> <s3_bucket> <s3_prefix> <aws_region> <project_name> <board> <project_dir> [custom_name]")
        sys.exit(1)

    firmware_path = sys.argv[1]
    s3_bucket = sys.argv[2]
    s3_prefix = sys.argv[3]
    aws_region = sys.argv[4]
    project_name = sys.argv[5]
    board = sys.argv[6]
    project_dir = sys.argv[7]
    
    custom_name = ""
    if len(sys.argv) >= 9:
        custom_name = sys.argv[8]

    # Import boto3 (should work in this environment)
    try:
        import boto3
        from botocore.exceptions import ClientError, NoCredentialsError
    except ImportError as e:
        print(f"ERROR: boto3 not installed in Python {sys.executable}")
        print(f"       ImportError: {e}")
        print(f"       Install with: {sys.executable} -m pip install boto3")
        sys.exit(1)

    # Extract firmware version from main.cpp
    firmware_version = "unknown"
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

    firmware_name = os.path.basename(firmware_path)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if custom_name:
        base_name = custom_name
    else:
        base_name = os.path.splitext(firmware_name)[0]
    extension = os.path.splitext(firmware_name)[1]

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

        # Upload as "latest" version - DISABLED per user request
        
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
                        'board': board
                    }
                }
            )

        print("✅ Upload successful!")
        print("✅ Upload successful!")
        if firmware_version != "unknown":
            print(f"   Versioned:   s3://{s3_bucket}/{version_key}")
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

if __name__ == "__main__":
    main()
