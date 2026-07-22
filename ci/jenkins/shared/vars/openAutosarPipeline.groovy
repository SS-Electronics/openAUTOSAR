// SPDX-License-Identifier: MIT

def call(Map config = [:]) {
  def agentLabel = config.get('agentLabel', 'linux-fast')
  def model = config.get('model', 'model/examples/vehicle')
  def profile = config.get('profile', 'generic-oem')
  def deliveryProfile = config.get('deliveryProfile', 'generic-supplier')
  def target = config.get('target', 'qemu-x86_64')

  pipeline {
    agent { label agentLabel }

    options {
      timestamps()
      ansiColor('xterm')
    }

    stages {
      stage('Bootstrap') {
        steps {
          sh './scripts/bootstrap.sh'
        }
      }

      stage('Configure') {
        steps {
          sh "./scripts/configure.sh --target ${target} --profile dev"
        }
      }

      stage('Generate') {
        steps {
          sh "./scripts/generate.sh --model ${model}"
        }
      }

      stage('Yocto Layer Smoke') {
        steps {
          sh './scripts/validate-yocto-layer.sh'
        }
      }

      stage('Build') {
        steps {
          sh './scripts/build.sh'
        }
      }

      stage('Test') {
        steps {
          sh './scripts/test.sh'
        }
        post {
          always {
            junit allowEmptyResults: true, testResults: 'out/test-results/**/*.xml'
          }
        }
      }

      stage('Network Lab Smoke') {
        steps {
          sh './scripts/run-network-lab.sh'
          sh './scripts/deploy-qemu.sh --dry-run --image out/qemu/agl-qemux86-64-placeholder.wic'
        }
      }

      stage('License Scan') {
        steps {
          sh './ci/checks/license-scan.sh'
        }
      }

      stage('Compliance') {
        steps {
          sh './scripts/validate-compliance.sh'
        }
      }

      stage('Architecture Work Products') {
        steps {
          sh './scripts/validate-architecture-work-products.sh'
        }
      }

      stage('Architecture Decisions') {
        steps {
          sh './scripts/validate-architecture-decisions.sh'
        }
      }

      stage('Program Governance') {
        steps {
          sh './scripts/validate-program-governance.sh'
        }
      }

      stage('Implementation Plan') {
        steps {
          sh './scripts/validate-implementation-plan.sh'
        }
      }

      stage('Tool Confidence') {
        steps {
          sh './scripts/validate-tool-confidence.sh'
        }
      }

      stage('API ABI') {
        steps {
          sh './scripts/validate-api-abi.sh'
        }
      }

      stage('Quality Metrics') {
        steps {
          sh './scripts/validate-quality-metrics.sh'
        }
      }

      stage('Classic Integration') {
        steps {
          sh './scripts/validate-classic-integration.sh'
        }
      }

      stage('Delivery Workflow') {
        steps {
          sh './scripts/validate-delivery-workflow.sh --source-only'
        }
      }

      stage('Package') {
        steps {
          sh "./scripts/package.sh --profile ${profile} --target ${target} " +
            "--model ${model} --delivery-profile ${deliveryProfile}"
        }
      }

      stage('Debug Bundle') {
        steps {
          sh "./scripts/export-debug-bundle.sh --profile ${profile} --target ${target}"
        }
      }

      stage('Supplier Delivery') {
        steps {
          sh "./scripts/export-supplier-delivery.sh --profile ${profile} " +
            "--delivery-profile ${deliveryProfile} --target ${target} --model ${model}"
        }
      }
    }
  }
}
